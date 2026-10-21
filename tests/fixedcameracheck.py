# Fixed camera, through the GUI: the Add component picker gives a camera the
# defaults (H 120, 4 layers, 16 bpp); Layers and Show at start are edited;
# Bake renders its view into bakes/<scene>-<object>.bgbake with the image and
# the layer mask beside it (320x240, the image not one flat colour, the mask
# no more layers than asked for) and points the component at it; the status
# says Baked and no "changed" warning shows. With a Fixed camera in the scene
# a mesh offers Keep 3D in fixed cameras. Moving a pillar in the saved scene
# makes the next load warn that the scene changed since the bake. Look
# through prints where the viewport went.
# usage: python3 fixedcameracheck.py <splashedit> [screenshot]   (needs a display: xvfb-run -a)
import json, os, re, shutil, struct, subprocess, sys, tempfile, zlib

editor = os.path.abspath(sys.argv[1])
keep = sys.argv[2] if len(sys.argv) > 2 else None
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
fails = 0
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')
W = lambda n: ['--wait', str(n)]
BAKE = 'bakes/courtyard-Yard_cam.bgbake'


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def png_rgba(path):
    """(width, height, flat list of RGBA tuples) of an 8-bit RGBA PNG."""
    data = open(path, 'rb').read()
    pos, idat, w, h = 8, b'', 0, 0
    while pos < len(data):
        n, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
            if depth != 8 or ctype != 6:
                return w, h, []
        elif kind == b'IDAT':
            idat += body
        pos += 12 + n
    raw, bpp, stride, px, prev = zlib.decompress(idat), 4, w * 4, [], bytearray(w * 4)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b, c = prev[i], prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        px += [tuple(line[i:i + 4]) for i in range(0, stride, 4)]
        prev = line
    return w, h, px


def fixture():
    project = os.path.join(tempfile.mkdtemp(), 'courtyard')
    shutil.copytree(example, project)
    scene = os.path.join(project, 'courtyard.scene')
    s = json.load(open(scene, encoding='utf-8'))
    s['objects'].append({'name': 'Yard cam', 'active': True, 'transform': {'position': [0, 1.6, -4.5],
                         'rotation': [0.0715, -0.2037, 0.0149, 0.9763], 'scale': [1, 1, 1]}, 'components': [], 'children': []})
    json.dump(s, open(scene, 'w', encoding='utf-8'), indent=2)
    return project


def run(project, select, args, shot):
    cmd = [editor, '--size', '1600x960', '--project', project, '--select', select] + W(20) + ['--wait-export'] + args + \
        ['--key', 'ctrl+s'] + W(10) + ['--wait-export'] + W(5) + ['--move', '760,940'] + W(10) + ['--screenshot', shot]
    r = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=280)
    lines = (r.stdout + r.stderr).strip().splitlines()
    missing = [l for l in lines if 'no such control drawn' in l]
    check(os.path.basename(shot) + ': every control the script names was drawn', not missing, missing)
    s = json.load(open(os.path.join(project, 'courtyard.scene'), encoding='utf-8'))
    return s, lines


def obj(s, name):
    def walk(objs):
        for o in objs:
            if o.get('name') == name:
                return o
            f = walk(o.get('children', []))
            if f:
                return f
    return walk(s['objects']) or {}


def comp(o, kind):
    return next((c for c in o.get('components', []) if c.get('type') == kind), None)


def typed(name, text):
    return ['--scroll-to', '@' + name, '--dblclick', '@' + name] + W(3) + ['--key', 'ctrl+a', '--text', text, '--key', 'enter'] + W(3)


project = fixture()
shot = keep or os.path.join(project, 'fixedcam.png')
ADD = ['--click', '@obj.addcomp'] + W(3) + ['--text', 'fixed camera'] + W(2) + ['--key', 'enter'] + W(5)
EDIT = typed('obj.fclayers', '2') + ['--scroll-to', '@obj.fcstart', '--click', '@obj.fcstart'] + W(3)
BAKE_ARGS = ['--scroll-to', '@fcbake', '--click', '@fcbake'] + W(10) + ['--wait-export'] + W(5)
s, out = run(project, 'Yard cam', ADD + EDIT + BAKE_ARGS + ['--scroll-to', '@fclook', '--click', '@fclook'] + W(5), shot)

# 1. The component and its edits, saved.
fc = comp(obj(s, 'Yard cam'), 'fixedCamera')
check('the picker adds a Fixed camera', fc is not None, obj(s, 'Yard cam'))
fc = fc or {}
check('defaults kept where not edited: H 120, 16 bpp', fc.get('h') == 120 and fc.get('bitDepth') == 16, fc)
check('Layers and Show at start were edited', fc.get('layers') == 2 and fc.get('showAtStart') is True, fc)
check('Bake points the component at its bake file', fc.get('bake') == BAKE, fc.get('bake'))

# 2. The files.
bake = os.path.join(project, BAKE)
meta = json.load(open(bake)) if os.path.exists(bake) else {}
check('the bake file is a background bake of 320x240',
      meta.get('format') == 'splashedit-ng/background-bake' and meta.get('width') == 320 and meta.get('height') == 240, meta)
check('it carries an 8-digit scene hash', re.fullmatch(r'[0-9a-f]{8}', meta.get('hash', '')) is not None, meta.get('hash'))
check('no more layers than asked for, each with pixels and an increasing depth',
      1 <= len(meta.get('layers', [])) <= 2 and all(l['pixels'] > 0 and l['nearSZ'] <= l['farSZ'] for l in meta.get('layers', [])) and
      [l['nearSZ'] for l in meta.get('layers', [])] == sorted(l['nearSZ'] for l in meta.get('layers', [])), meta.get('layers'))
img = os.path.join(os.path.dirname(bake), meta.get('image', 'missing.png'))
w, h, px = png_rgba(img) if os.path.exists(img) else (0, 0, [])
check('the image is 320x240', (w, h) == (320, 240), (w, h))
colours = len(set(p[:3] for p in px[::7]))
check('the image is the scene, not one flat colour', colours > 200, colours)
mask = os.path.join(os.path.dirname(bake), meta.get('layerMask', 'missing.png'))
mw, mh, mpx = png_rgba(mask) if os.path.exists(mask) else (0, 0, [])
levels = sorted(set(p[0] for p in mpx))
check('the layer mask is 320x240 with layers 0 (image only) to 2', (mw, mh) == (320, 240) and levels and levels[0] == 0 and levels[-1] <= 2,
      ((mw, mh), levels))
inLayers = sum(1 for p in mpx if p[0])
check('the layer pixel counts in the file match the mask', inLayers == sum(l['pixels'] for l in meta.get('layers', [])),
      (inLayers, meta.get('layers')))

# 3. What the editor said.
baked = [l for l in out if l.startswith('fixedcam: baked ' + BAKE)]
check('the bake reports its file and layers', len(baked) == 1, [l for l in out if l.startswith('fixedcam:')])
rep = [l for l in out if l.startswith('fixedcam: Yard cam ')]
check('the inspector line reports the settings and Baked',
      rep and rep[-1].startswith('fixedcam: Yard cam h 120 fov 90 layers 2 bpp 16 start on bake ' + BAKE + ' status Baked, ') and
      'changed' not in rep[-1], rep[-1:])
check('Look through moved the viewport', 'fixedcam: look through Yard cam' in out, [l for l in out if 'look through' in l])

# 4. Keep 3D is offered on a mesh once there is a Fixed camera.
s2, out2 = run(project, 'Crate', ['--scroll-to', '@obj.keep3d', '--click', '@obj.keep3d'] + W(3), os.path.join(project, 'keep.png'))
check('Keep 3D in fixed cameras saves on the mesh', (comp(obj(s2, 'Crate'), 'mesh') or {}).get('keep3dInFixedCameras') is True,
      comp(obj(s2, 'Crate'), 'mesh'))

# 5. A baked object moves: the bake is stale.
s3 = json.load(open(os.path.join(project, 'courtyard.scene'), encoding='utf-8'))
obj(s3, 'Pillar')['transform']['position'][0] += 0.5
json.dump(s3, open(os.path.join(project, 'courtyard.scene'), 'w', encoding='utf-8'), indent=2)
_, out3 = run(project, 'Yard cam', [], os.path.join(project, 'stale.png'))
rep3 = [l for l in out3 if l.startswith('fixedcam: Yard cam ')]
check('after moving a pillar the inspector says the scene changed', rep3 and 'the scene changed since, bake again' in rep3[-1], rep3[-1:])
probs = [l for l in out3 if l.startswith('problems: ')]
check('and Problems counts the warning', probs and 'warning' in probs[-1], probs[-1:])

print('%d failure%s' % (fails, '' if fails == 1 else 's'))
sys.exit(1 if fails else 0)
