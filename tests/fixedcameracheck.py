# Fixed camera, through the GUI: the Add component picker gives a camera the
# defaults (H 120, 4 layers, 16 bpp); Layers and Show at start are edited;
# Bake renders its view into bakes/<scene>-<object>.bgbake with the image and
# the occluder depth beside it (320x240, the image not one flat colour, the
# depth file's occluder count the one in the .bgbake) and points the
# component at it; the status says Baked with the last export's layer tiles
# and VRAM, and no "changed" warning shows. With a Fixed camera in the scene
# a mesh offers Keep 3D in fixed cameras. Moving a pillar in the saved scene
# makes the next load warn that the scene changed since the bake. Look
# through puts the viewport at the camera with its field of view: the clean
# viewport's edges land where the bake's are.
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
    """(width, height, flat list of RGBA tuples) of an 8-bit RGBA or RGB PNG."""
    data = open(path, 'rb').read()
    pos, idat, w, h = 8, b'', 0, 0
    while pos < len(data):
        n, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
            if depth != 8 or ctype not in (2, 6):
                return w, h, []
            bpp = 4 if ctype == 6 else 3
        elif kind == b'IDAT':
            idat += body
        pos += 12 + n
    raw, stride, px, prev = zlib.decompress(idat), w * bpp, [], bytearray(w * bpp)
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
        px += [tuple(line[i:i + 3]) + ((line[i + 3],) if bpp == 4 else (255,)) for i in range(0, stride, bpp)]
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
img = os.path.join(os.path.dirname(bake), meta.get('image', 'missing.png'))
w, h, px = png_rgba(img) if os.path.exists(img) else (0, 0, [])
check('the image is 320x240', (w, h) == (320, 240), (w, h))
colours = len(set(p[:3] for p in px[::7]))
check('the image is the scene, not one flat colour', colours > 200, colours)
depth = os.path.join(os.path.dirname(bake), meta.get('depth', 'missing.depth'))
raw = open(depth, 'rb').read() if os.path.exists(depth) else b''
check('the depth file is 320x240 16-bit', len(raw) == 320 * 240 * 2, len(raw))
values = struct.unpack('<%dH' % (len(raw) // 2), raw) if raw else ()
occ = sum(1 for v in values if v)
check('its occluder count is the one in the bake file, and leaves sky and floor out',
      occ == meta.get('occluders') and 0 < occ < 320 * 240 * 0.8, (occ, meta.get('occluders')))

# 3. What the editor said.
baked = [l for l in out if re.match(r'fixedcam: baked ' + re.escape(BAKE) + r' occluders \d+ layer tiles [1-9]\d*$', l)]
check('the bake reports its file, occluders and layer tiles', len(baked) == 1, [l for l in out if l.startswith('fixedcam:')])
rep = [l for l in out if l.startswith('fixedcam: Yard cam ')]
check('the inspector line reports the settings and Baked',
      rep and re.match(r'fixedcam: Yard cam h 120 fov 90 layers 2 bpp 16 start on bake ' + re.escape(BAKE) +
                       r' status Baked, [1-9]\d* layer tiles, \d+ KB VRAM \(\d+% of it\)$', rep[-1]) is not None, rep[-1:])
check('Look through moved the viewport with the camera\'s 90 degrees', 'fixedcam: look through Yard cam fov 90' in out,
      [l for l in out if 'look through' in l])

# 3b. Look through frames what the bake shows: the clean viewport, scaled to
# 240 rows and cut to the middle 320 columns, has its edges where the bake has
# them (best match within a pixel of no shift).
lt_shot = os.path.join(project, 'look.png')
r = subprocess.run([editor, '--size', '1600x960', '--project', project, '--select', 'Yard cam', '--no-overlays', '--clean'] + W(20) +
                   ['--scroll-to', '@fclook', '--click', '@fclook'] + W(10) + ['--move', '760,940'] + W(5) + ['--screenshot', lt_shot],
                   capture_output=True, text=True, env=ENV, timeout=280)
rect = None
for l in (r.stdout + r.stderr).splitlines():
    m = re.match(r'control vp\.view at (-?\d+),(-?\d+) (-?\d+),(-?\d+)$', l)
    if m: rect = tuple(int(v) for v in m.groups())
sw, sh, spx = png_rgba(lt_shot) if os.path.exists(lt_shot) else (0, 0, [])
def luma_grid(px, w, x0, y0, x1, y1, ow, oh):
    # Nearest sample of the rect onto ow x oh, as luma.
    out = []
    for oy in range(oh):
        sy = y0 + (oy + 0.5) * (y1 - y0) / oh
        row = []
        for ox in range(ow):
            sx = x0 + (ox + 0.5) * (x1 - x0) / ow
            p = px[int(sy) * w + int(sx)]
            row.append(0.3 * p[0] + 0.59 * p[1] + 0.11 * p[2])
        out.append(row)
    return out
def edges(g):
    h, w = len(g), len(g[0])
    e = [[False] * w for _ in range(h)]
    for y in range(1, h - 1):
        for x in range(1, w - 1):
            e[y][x] = abs(g[y][x + 1] - g[y][x - 1]) + abs(g[y + 1][x] - g[y - 1][x]) > 40
    return e
if rect and spx and px:
    x0, y0, x1, y1 = rect
    vh = y1 - y0
    vw = vh * 320 / 240  # the bake's 4:3 in the middle of the panel
    cx = (x0 + x1) / 2
    view = edges(luma_grid(spx, sw, cx - vw / 2, y0, cx + vw / 2, y1, 320, 240))
    bake_l = [[0.3 * p[0] + 0.59 * p[1] + 0.11 * p[2] for p in px[y * 320:(y + 1) * 320]] for y in range(240)]
    be = edges(bake_l)
    best, scores = None, {}
    for dy in range(-3, 4):
        for dx in range(-3, 4):
            inter = uni = 0
            for y in range(8, 232):
                for x in range(8, 312):
                    a, b = view[y][x], be[y + dy][x + dx]
                    inter += a and b
                    uni += a or b
            scores[(dx, dy)] = inter / max(1, uni)
    best = max(scores, key=scores.get)
    check('Look through frames the bake: edges best aligned within a pixel', abs(best[0]) <= 1 and abs(best[1]) <= 1 and scores[best] > 0.3,
          (best, round(scores[best], 3), round(scores[(0, 0)], 3)))
else:
    check('Look through screenshot and viewport rect', False, (rect, bool(spx), bool(px)))

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
