# Blob shadows in the scene view are depth tested and kept on what the caster
# stands on. The courtyard example with a 2 x 1 x 1.5 m stone table and a small
# crate on it (Table Crate, its bottom on the table top, no nav floor there):
# one run, one camera framed on the crate, three shots of the same frame:
#   hidden  the crate switched off (its pixels are the ones that change)
#   off     the crate on, Shadow = Off
#   blob    Shadow = Blob, Size 1.5 m (`shadow preview: Table Crate floor
#           bounds height 0.00 radius 1.50 darkness 153`)
# A pixel is darkened when it is darker in "blob" than in "off". Then:
#   (a) the crate's own pixels are not darkened: the blob is under it;
#   (b) the table top around it is (the blob is still drawn);
#   (c) the table's front and right faces below the top edge are not: the
#       1.5 m rim would hang 0.5 m past the table's edges, and is kept to the
#       table's X/Z footprint as the console keeps it.
# Empty objects at the table's corners give their screen points as
# vp.marker.<name>, so the faces are found without the camera's maths.
# usage: python3 blobdepthcheck.py <splashedit> [screenshot.png]   (needs a display: xvfb-run -a)
import copy, json, os, re, shutil, struct, subprocess, sys, tempfile, zlib

editor = os.path.abspath(sys.argv[1])
keep_shot = sys.argv[2] if len(sys.argv) > 2 else None
here = os.path.dirname(os.path.abspath(__file__))
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')
fails = 0


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def read_png(path):
    """(width, height, rows of RGB bytes) of an 8-bit RGBA or RGB PNG."""
    data = open(path, 'rb').read()
    pos, idat, w, h, bpp = 8, b'', 0, 0, 3
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


def sums(rows, box):
    """R+G+B of every pixel in the box (x0, y0, x1, y1), row by row."""
    x0, y0, x1, y1 = box
    return [[r[3 * x] + r[3 * x + 1] + r[3 * x + 2] for x in range(x0, x1)] for r in rows[y0:y1]]


def erode(mask, k):
    """Pixels whose whole (2k+1)^2 square is in the mask."""
    def runs(m):
        out = []
        for row in m:
            pre = [0]
            for v in row: pre.append(pre[-1] + v)
            n = len(row)
            out.append([int(x >= k and x + k < n and pre[x + k + 1] - pre[x - k] == 2 * k + 1) for x in range(n)])
        return out
    t = runs(mask)
    t = runs([list(c) for c in zip(*t)])
    return [list(c) for c in zip(*t)]


def inside(poly, x, y, inset):
    """(x, y) inside the convex polygon at least `inset` pixels from each edge."""
    sign = 0
    for i in range(len(poly)):
        (ax, ay), (bx, by) = poly[i], poly[(i + 1) % len(poly)]
        c = (bx - ax) * (y - ay) - (by - ay) * (x - ax)
        if sign == 0: sign = 1 if c > 0 else -1
        d = c * sign / max(1e-6, ((bx - ax) ** 2 + (by - ay) ** 2) ** 0.5)
        if d < inset: return False
    return True


# The project: the courtyard example, its crates replaced by the table and the
# crate on it, and an empty object at six of the table's corners.
root = tempfile.mkdtemp()
proj = os.path.join(root, 'p')
shutil.copytree(os.path.join(here, '..', 'examples', 'courtyard'), proj)
scene = os.path.join(proj, 'courtyard.scene')
d = json.load(open(scene))
crates = next(o for o in d['objects'] if o['name'] == 'Crates')
table = copy.deepcopy(crates['children'][0])
table['name'] = 'Table'
table['transform'] = {'position': [0.0, 0.5, 1.0], 'rotation': [0, 0, 0, 1], 'scale': [2.0, 1.0, 1.5]}
table['components'][0]['materials'][0]['texture'] = 'textures/stone.png'
crate = copy.deepcopy(crates['children'][0])
crate['name'] = 'Table Crate'
crate['transform'] = {'position': [0.0, 1.3, 1.0], 'rotation': [0.0, -0.13052619, 0.0, 0.9914449], 'scale': [0.6, 0.6, 0.6]}
crates['children'] = [table, crate]
corners = {'TFL': (-1, 1, 0.25), 'TFR': (1, 1, 0.25), 'TBR': (1, 1, 1.75),
           'MFL': (-1, 0.6, 0.25), 'MFR': (1, 0.6, 0.25), 'MBR': (1, 0.6, 1.75)}
for n, p in corners.items():
    d['objects'].append({'name': 'Corner ' + n, 'active': True, 'children': [],
                         'transform': {'position': list(p), 'rotation': [0, 0, 0, 1], 'scale': [1, 1, 1]}})
json.dump(d, open(scene, 'w'), indent=2)

hidden, off = os.path.join(root, 'hidden.png'), os.path.join(root, 'off.png')
shot = keep_shot or os.path.join(root, 'blob.png')
W = lambda n: ['--wait', str(n)]
# The mouse rests on the back wall, off the gizmo, so the wheel reaches the
# view (it backs the camera off until the table's front corners are in it).
# The object's on/off switch has no name of its own: it is level with the
# name at the inspector's right edge.
settle = ['--wait-export'] + W(10) + ['--move', '1000,250'] + W(5)
cmd = [editor, '--size', '1600x960', '--project', proj, '--scene', scene, '--select', 'Table Crate'] + W(30) + \
      ['--wait-export', '--move', '1000,250'] + W(3) + ['--key', 'f'] + W(10) + ['--wheel', '1000,250,-5'] + W(10) + \
      ['--scroll-to', '@obj.name'] + W(5) + ['--click', '@obj.name,226,10'] + W(10) + settle + ['--shot', hidden] + \
      ['--scroll-to', '@obj.name'] + W(5) + ['--click', '@obj.name,226,10'] + W(10) + settle + ['--shot', off] + \
      ['--scroll-to', '@obj.shadow'] + W(5) + ['--click', '@obj.shadow'] + W(5) + ['--click', '@obj.shadow/Blob'] + W(10) + \
      ['--scroll-to', '@obj.shadowsize'] + W(5) + ['--dblclick', '@obj.shadowsize'] + W(3) + ['--key', 'ctrl+a', '--text', '1.5', '--key', 'enter'] + W(10) + \
      settle + ['--screenshot', shot]
r = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=600)
lines = (r.stdout + r.stderr).splitlines()
rects = {}
for l in lines:
    m = re.match(r'control (\S+(?: \S+)*) at (-?\d+),(-?\d+) (-?\d+),(-?\d+)$', l)
    if m: rects[m.group(1)] = tuple(int(v) for v in m.groups()[1:])
out = [l for l in lines if not l.startswith('control ')]

check('the editor ran to its screenshot', r.returncode == 0 and all(os.path.exists(p) for p in (hidden, off, shot)),
      (r.returncode, out[-10:]))
preview = [l for l in out if l.startswith('shadow preview: Table Crate ')]
check('the crate has a 1.5 m blob on its bounds bottom, the table top',
      preview[-1:] == ['shadow preview: Table Crate floor bounds height 0.00 radius 1.50 darkness 153'], preview)
view, box = rects.get('vp.view'), rects.get('vp.shadow.Table Crate')
pts = {n: ((rects[k][0] + rects[k][2]) / 2, (rects[k][1] + rects[k][3]) / 2)
       for n in corners for k in ['vp.marker.Corner ' + n] if k in rects}
check('the blob and the table\'s front corners are on screen', bool(view and box) and {'TFL', 'TFR', 'MFL', 'MFR'} <= set(pts),
      (view, box, sorted(pts)))
if fails:
    print('blobdepthcheck: %d failed' % fails)
    sys.exit(1)

vx0, vy0, vx1, vy1 = view
S = {}
for name, path in (('hidden', hidden), ('off', off), ('blob', shot)):
    S[name] = sums(read_png(path)[2], view)
H, Wd = vy1 - vy0, vx1 - vx0
dark = [[int(S['off'][y][x] - S['blob'][y][x] >= 6) for x in range(Wd)] for y in range(H)]
crate_raw = [[int(abs(S['off'][y][x] - S['hidden'][y][x]) > 24) for x in range(Wd)] for y in range(H)]
# The crate less its outermost 4 screen pixels (a little over one PS1 pixel):
# its silhouette's edge pixels may show the table behind it, and the nav lines
# drawn over the table move a little when the crate is off.
crate_px = erode(crate_raw, 4)
n_crate = sum(map(sum, crate_px))
a = sum(dark[y][x] for y in range(H) for x in range(Wd) if crate_px[y][x])
bx0, by0, bx1, by1 = box
b = sum(dark[y - vy0][x - vx0] for y in range(max(by0, vy0), min(by1, vy1)) for x in range(max(bx0, vx0), min(bx1, vx1))
        if not crate_raw[y - vy0][x - vx0])
faces = [('front', ['TFL', 'TFR', 'MFR', 'MFL']), ('right', ['TFR', 'TBR', 'MBR', 'MFR'])]
c = n_face = 0
for fname, ks in faces:
    if not set(ks) <= set(pts): continue
    poly = [(pts[k][0] - vx0, pts[k][1] - vy0) for k in ks]
    xs, ys = [p[0] for p in poly], [p[1] for p in poly]
    for y in range(max(0, int(min(ys))), min(H, int(max(ys)) + 1)):
        for x in range(max(0, int(min(xs))), min(Wd, int(max(xs)) + 1)):
            if inside(poly, x + 0.5, y + 0.5, 4) and not crate_raw[y][x]:
                n_face += 1
                c += dark[y][x]
print('pixels: crate %d darkened %d; around the crate darkened %d; table faces %d darkened %d' % (n_crate, a, b, n_face, c))
check('the crate has pixels on screen', n_crate > 1500, n_crate)
check('(a) the blob does not darken the crate casting it', a <= n_crate // 200, (a, n_crate))
check('(b) the blob darkens the table top around the crate', b > 2000, b)
check('(c) the blob does not hang over the table\'s edges onto its faces', n_face > 1000 and c <= n_face // 200, (c, n_face))
print('blobdepthcheck: %s' % ('all passed' if not fails else '%d failed' % fails))
sys.exit(1 if fails else 0)
