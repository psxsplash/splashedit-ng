# The canvas inspector's Blend row (#312) driven headless: picking a mode
# from the dropdown saves it, is one undo step, and a text element shows the
# row disabled (no menu, nothing saved). The canvas preview blends: pixels of
# a Subtract box over a white stripe come out darker than the stripe.
# usage: python3 uiblendcheck.py <splashedit> [screenshot-dir]
import json, os, re, struct, subprocess, sys, tempfile, zlib

editor = os.path.abspath(sys.argv[1])
shots = sys.argv[2] if len(sys.argv) > 2 else None
fails = 0
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')
MODES = ['half', 'add', 'subtract', 'addQuarter']
LABELS = {'half': '50% (half)', 'add': 'Add', 'subtract': 'Subtract', 'addQuarter': 'Add 25%'}
STRIPES = [[1, 0.2, 0.2], [0.2, 0.9, 0.3], [0.2, 0.4, 1], [1, 0.9, 0.2], [1, 1, 1]]


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def box(name, rect, color, **kw):
    e = {'type': 'box', 'name': name, 'visible': True, 'rect': rect, 'anchorMin': [0, 0], 'anchorMax': [0, 0], 'color': color}
    e.update(kw)
    return e


def row(i):
    return [10, 30 + i * 45, 300, 35]


def project(blends=None):
    """A canvas 'hud': five opaque stripes, then one grey box per row across
    them and the checkerboard on both sides (blend from `blends`), and a text
    label on each row."""
    d = os.path.join(tempfile.mkdtemp(), 'hud')
    os.makedirs(d)
    els = [box('stripe%d' % i, [40 + i * 48, 20, 48, 200], c) for i, c in enumerate(STRIPES)]
    for i, m in enumerate(MODES):
        extra = {'blend': blends[i]} if blends and blends[i] != 'opaque' else {}
        els.append(box('row%d' % i, row(i), [0.5, 0.5, 0.5], **extra))
    for i, m in enumerate(MODES):
        r = row(i)
        els.append({'type': 'text', 'name': 'label%d' % i, 'visible': True, 'rect': [r[0] + 4, r[1] + 2, 120, 16],
                    'anchorMin': [0, 0], 'anchorMax': [0, 0], 'color': [1, 1, 1], 'text': LABELS[m] if blends else 'Opaque',
                    'font': None})
    s = {'format': 'splashedit-ng/scene', 'version': 1, 'settings': {}, 'objects': [],
         'canvases': [{'name': 'hud', 'visible': True, 'sortOrder': 0, 'defaultFont': None, 'elements': els}]}
    json.dump(s, open(os.path.join(d, 'hud.scene'), 'w'), indent=1)
    return d


def readpng(path):
    d = open(path, 'rb').read()
    p, idat = 8, b''
    while p < len(d):
        n, = struct.unpack_from('>I', d, p)
        t = d[p + 4:p + 8]
        if t == b'IHDR':
            w, h, depth, ctype = struct.unpack_from('>IIBB', d, p + 8)
            bpp = 3 if ctype == 2 else 4
        if t == b'IDAT':
            idat += d[p + 8:p + 8 + n]
        p += 12 + n
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
                p_ = a + b - c
                pa, pb, pc = abs(p_ - a), abs(p_ - b), abs(p_ - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    return w, h, bpp, rows


def run(args, d=None, shot=None):
    d = d or project()
    shot = shot or os.path.join(d, 'shot.png')
    cmd = [editor, '--size', '1600x960', '--project', d, '--ui', '--wait', '5'] + args + [
        '--key', 'ctrl+s', '--wait', '5', '--screenshot', shot]
    r = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=180)
    out = (r.stdout + r.stderr).strip()
    scene = json.load(open(os.path.join(d, 'hud.scene'), encoding='utf-8'))
    controls = {m.group(1): tuple(map(int, m.groups()[1:])) for m in re.finditer(r'^control (.+?) at (-?\d+),(-?\d+) (-?\d+),(-?\d+)$', out, re.M)}
    return d, scene, out, controls


def blends(scene):
    return {e['name']: e.get('blend') for e in scene['canvases'][0]['elements'] if 'blend' in e}


d, scene, out, ctl = run([])
m = re.search(r'uicanvas: screen ([\d.]+),([\d.]+) scale ([\d.]+)', out)
check('--ui opens the UI tab and reports its layout', m is not None, out[-300:])
if not m:
    sys.exit(1)
X, Y, S = map(float, m.groups())


def at(px, py):
    return '%d,%d' % (round(X + (px + 0.5) * S), round(Y + (py + 0.5) * S))


ROW2 = at(290, row(2)[1] + 30)  # row 2, right of the stripes (over the checkerboard)
PICK = ['--click', ROW2, '--wait', '5', '--click', '@ui.blend', '--wait', '5']
d, scene, out, ctl = run(PICK)
check('a Box inspector draws the Blend row', 'ui.blend' in ctl, sorted(k for k in ctl if k.startswith('ui.'))[:30])
check('the Blend menu lists the five modes',
      all('ui.blend/' + l in ctl for l in ['Opaque', '50% (half)', 'Add', 'Subtract', 'Add 25%']),
      sorted(k for k in ctl if k.startswith('ui.blend')))
d, scene, out, ctl = run(PICK + ['--click', '@ui.blend/Subtract', '--wait', '5'])
check('picking Subtract saves "blend": "subtract"', blends(scene) == {'row2': 'subtract'}, blends(scene))
d, scene, out, ctl = run(PICK + ['--click', '@ui.blend/Subtract', '--wait', '5', '--click', '@ui.blend', '--wait', '5',
                                 '--click', '@ui.blend/Add', '--wait', '5', '--key', 'ctrl+z', '--wait', '5'])
check('one pick is one undo step', blends(scene) == {'row2': 'subtract'}, blends(scene))
d, scene, out, ctl = run(PICK + ['--click', '@ui.blend/Subtract', '--wait', '5', '--key', 'ctrl+z', '--wait', '5'])
check('undo puts back Opaque (no "blend" key)', blends(scene) == {}, blends(scene))

# A text element: the row is there, but no menu opens and nothing is saved.
LABEL = at(row(1)[0] + 8, row(1)[1] + 8)
d, scene, out, ctl = run(['--click', LABEL, '--wait', '5', '--click', '@ui.blend', '--wait', '5'])
check('a Text inspector draws the Blend row', 'ui.blend' in ctl, sorted(k for k in ctl if k.startswith('ui.'))[:30])
check('a Text Blend row opens no menu', not any(k.startswith('ui.blend#menu') or k.startswith('ui.blend/') for k in ctl),
      sorted(k for k in ctl if k.startswith('ui.blend')))
check('a Text Blend row saves nothing', blends(scene) == {}, blends(scene))

# The preview: each mode over the white stripe (x 232..279), against the
# opaque grey row and the stripe itself, in the editor's own screenshot.
d = project(MODES)
shot = os.path.join(shots, 'se-uiblend-canvas.png') if shots else None
d, scene, out, ctl = run(['--click', at(290, row(2)[1] + 30), '--wait', '5'], d, shot)
w, h, bpp, rows = readpng(shot or os.path.join(d, 'shot.png'))


def px(cx, cy):
    x, y = round(X + (cx + 0.5) * S), round(Y + (cy + 0.5) * S)
    return tuple(rows[y][x * bpp:x * bpp + 3])


white = px(256, 25)  # the white stripe above the rows
got = {m: px(256, row(i)[1] + 25) for i, m in enumerate(MODES)}
check('the white stripe is white', min(white) >= 240, white)
ex = lambda c5: (c5 << 3) | (c5 >> 2)  # grey 0.5 is 16 of 31, white 31: the GPU blends 5-bit channels
check('Half over white is the average of grey and white', all(c == ex((31 + 16) // 2) for c in got['half']), got['half'])
check('Add over white stays white', all(c >= 250 for c in got['add']), got['add'])
check('Subtract over white is white less grey', all(c == ex(31 - 16) for c in got['subtract']), got['subtract'])
check('Add 25% over white stays white', all(c >= 250 for c in got['addQuarter']), got['addQuarter'])
print(f'{fails} failures')
sys.exit(1 if fails else 0)
