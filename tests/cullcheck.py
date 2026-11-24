# The Scene view culls back faces as psxsplash does (#386), and draws
# wireframe on request. A fresh project with two 4 x 4 quads made of the same
# corners in the same order, side by side at z = 0: Front (red) with its
# normals toward the camera (-Z), Back (blue) with them away (+Z). The
# exporter turns each triangle to agree with its normals and the console's
# NCLIP draws the ones clockwise on screen, so in game Front shows and Back
# does not. By default (`scene view: cull back faces on, wireframe off`) the
# viewport shows red and no blue; with the toolbar's vp.cull clicked, or
# --no-cull, both; clicked twice, red only again. vp.wire draws the edges: far
# fewer red pixels than the filled quad, still no blue.
# usage: python3 cullcheck.py <splashedit> [screenshot.png]   (needs a display: xvfb-run -a)
import json, os, re, struct, subprocess, sys, tempfile, zlib

editor = os.path.abspath(sys.argv[1])
keep_shot = sys.argv[2] if len(sys.argv) > 2 else None
fails = 0
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')
GREEN = (0x20, 0xc0, 0x40)


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def write_png(path, w, h, px):
    """8-bit RGBA PNG; px(x, y) -> (r, g, b, a), y = 0 the top row."""
    raw = b''.join(b'\0' + bytes(c for x in range(w) for c in px(x, y)) for y in range(h))
    chunk = lambda k, d: struct.pack('>I', len(d)) + k + d + struct.pack('>I', zlib.crc32(k + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def read_png(path):
    """(width, height, rows of RGB bytes) of an 8-bit RGBA or RGB PNG."""
    data = open(path, 'rb').read()
    pos, idat, w = 8, b'', 0
    while pos < len(data):
        n, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
            bpp = 4 if ctype == 6 else 3
        elif kind == b'IDAT':
            idat += body
        pos += 12 + n
    raw, stride, rows, prev = zlib.decompress(idat), w * bpp, [], bytearray(w * bpp)
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
        rows.append(bytes(line[i] for i in range(stride) if i % bpp < 3))
        prev = line
    return w, h, rows


def project():
    """A temp project: cull.scene with Front (red, normals -Z) at x = -3 and
    Back (blue, normals +Z) at x = 3, on a green background."""
    root = tempfile.mkdtemp()
    os.makedirs(os.path.join(root, 'meshes'))
    os.makedirs(os.path.join(root, 'textures'))
    objects = []
    for name, nz, colour, x in (('Front', -1, (255, 0, 0, 255), -3), ('Back', 1, (0, 0, 255, 255), 3)):
        write_png(os.path.join(root, 'textures', name + '.png'), 16, 16, lambda px, py, c=colour: c)
        mesh = {'format': 'splashedit-ng/mesh', 'version': 1,
                'positions': [-2, -2, 0, 2, -2, 0, 2, 2, 0, -2, 2, 0],
                'normals': [0, 0, nz] * 4,
                'uv': [0, 0, 1, 0, 1, 1, 0, 1],
                'submeshes': [[0, 1, 2, 0, 2, 3]]}
        json.dump(mesh, open(os.path.join(root, 'meshes', name + '.mesh'), 'w'), indent=1)
        objects.append({'name': name, 'active': True,
                        'transform': {'position': [x, 0, 0], 'rotation': [0, 0, 0, 1], 'scale': [1, 1, 1]},
                        'components': [{'type': 'mesh', 'mesh': 'meshes/' + name + '.mesh', 'bitDepth': 16, 'vertexColors': 'flat',
                                        'materials': [{'texture': 'textures/' + name + '.png', 'color': [1, 1, 1, 1]}]}],
                        'children': []})
    scene = {'format': 'splashedit-ng/scene', 'version': 1,
             'settings': {'gteScaling': 100.0, 'sceneType': 'exterior', 'networkId': '',
                          'fog': {'enabled': False, 'color': [c / 255 for c in GREEN], 'density': 5}},
             'objects': objects}
    json.dump(scene, open(os.path.join(root, 'cull.scene'), 'w'), indent=1)
    return root


def run(root, shot, args):
    """The editor on `root` with `args`, the pointer parked on the status
    bar: (last `scene view:` line, share of red and of blue viewport pixels)."""
    cmd = [editor, '--size', '1600x960', '--project', root, '--no-overlays', '--clean', '--wait', '10'] + args + \
          ['--move', '760,940', '--wait', '10', '--screenshot', shot]
    r = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=300)
    lines = (r.stdout + r.stderr).strip().splitlines()
    rects = {}
    for l in lines:
        m = re.match(r'control (\S+(?: \S+)*) at (-?\d+),(-?\d+) (-?\d+),(-?\d+)$', l)
        if m: rects[m.group(1)] = tuple(int(v) for v in m.groups()[1:])
    state = [l for l in lines if l.startswith('scene view: ')]
    for l in lines:
        if l.startswith('@'): print('editor: ' + l)  # a scripted control it could not find
    v = rects.get('vp.view')
    if not v or not os.path.exists(shot): return (state[-1] if state else None), None, None
    _, _, rows = read_png(shot)
    n = red = blue = 0
    # Below the floating toolbars, above the bottom chips.
    for y in range(v[1] + 60, v[3] - 50, 2):
        for x in range(v[0] + 4, v[2] - 4, 2):
            p = rows[y][3 * x:3 * x + 3]
            n += 1
            red += p[0] > 160 and p[1] < 80 and p[2] < 80
            blue += p[2] > 160 and p[0] < 80 and p[1] < 80
    return (state[-1] if state else None), red / max(n, 1), blue / max(n, 1)


root = project()
CLICK = lambda c: ['--click', '@' + c, '--wait', '10']
shot = keep_shot or os.path.join(root, 'default.png')
state, red, blue = run(root, shot, [])
check('cull back faces is on by default, wireframe off', state == 'scene view: cull back faces on, wireframe off', state)
check('the quad facing the camera shows', red is not None and red > 0.02, red)
check('the quad facing away is culled', blue is not None and blue < 0.0005, blue)
filled = red or 0

state, red, blue = run(root, os.path.join(root, 'off.png'), CLICK('vp.cull'))
check('vp.cull turns culling off', state == 'scene view: cull back faces off, wireframe off', state)
check('with culling off both quads show', red is not None and red > 0.02 and blue > 0.02, (red, blue))

state, red, blue = run(root, os.path.join(root, 'again.png'), CLICK('vp.cull') + CLICK('vp.cull'))
check('vp.cull twice turns it back on', state == 'scene view: cull back faces on, wireframe off', state)
check('back on, the quad facing away is culled again', red is not None and red > 0.02 and blue < 0.0005, (red, blue))

state, red, blue = run(root, os.path.join(root, 'nocull.png'), ['--no-cull'])
check('--no-cull starts with culling off', state == 'scene view: cull back faces off, wireframe off', state)
check('--no-cull shows both quads', red is not None and red > 0.02 and blue > 0.02, (red, blue))

state, red, blue = run(root, os.path.join(root, 'wire.png'), CLICK('vp.wire'))
check('vp.wire turns wireframe on', state == 'scene view: cull back faces on, wireframe on', state)
check('wireframe draws the edges, not the filled quad', red is not None and 0 < red < filled * 0.5, (red, filled))
check('wireframe keeps the culled quad hidden', blue is not None and blue < 0.0005, blue)

print('cullcheck: %s' % ('all passed' if not fails else '%d failed' % fails))
sys.exit(1 if fails else 0)
