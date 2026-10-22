# Shine (environment-mapped reflections) in the editor, on a courtyard copy
# with a sky picture, textures/env.png, added to the project. With the Crate
# selected its one material shows a Shine slot (obj.shine) and no strength or
# blend rows while it has no image (`material 0: ... shine none`). env.png
# dropped from the Project panel onto the slot sets it (`shine env.png
# strength 50 blend add`), the Shine strength and Shine blend rows appear,
# the scene view draws the overlay over the Crate's 12 triangles (`shine
# preview: 12 triangles`) and the Crate's screen box gets brighter, the sky
# being added. Typing 25 into Shine strength and picking Half from Shine blend
# take (`strength 25 blend half`); Ctrl+S saves "shine": {"texture":
# "textures/env.png", "strength": 0.25, "blend": "half"}, and splashpack-cli
# exports it as a v30 pack with one shine record for the Crate (strength 32 of
# 128, blend 1, 12 Tris) whose Tris carry flag 0x4.
# usage: python3 shinecheck.py <splashedit> [screenshot.png]   (needs a display: xvfb-run -a)
import json, os, re, shutil, struct, subprocess, sys, tempfile, zlib

editor = os.path.abspath(sys.argv[1])
keep_shot = sys.argv[2] if len(sys.argv) > 2 else None
cli = os.path.join(os.path.dirname(editor), 'cli', 'splashpack-cli')
here = os.path.dirname(os.path.abspath(__file__))
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')
# At 1600x960 with the Project panel at its default height (materialscheck's).
SEARCH = '880,731'  # the panel's search field
TILE = '530,812'    # the first file in the grid
fails = 0


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def write_png(path, w, h, pixel):
    raw = b''.join(b'\0' + bytes(c for x in range(w) for c in pixel(x, y)) for y in range(h))
    chunk = lambda kind, body: struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


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


def brightness(path, box):
    """Mean of R+G+B over the box (x0, y0, x1, y1) of a screenshot."""
    _, _, rows = read_png(path)
    x0, y0, x1, y1 = box
    s = n = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            s += sum(rows[y][3 * x:3 * x + 3])
            n += 1
    return s / max(n, 1)


def sky(x, y):
    # Blue sky over a white horizon over dark ground, up at the top.
    if y < 30: return (40 + 4 * y, 80 + 4 * y, 200)
    if y < 35: return (250, 245, 230)
    return (70, 55, 40)


root = tempfile.mkdtemp()
proj = os.path.join(root, 'p')
shutil.copytree(os.path.join(here, '..', 'examples', 'courtyard'), proj)
write_png(os.path.join(proj, 'textures', 'env.png'), 64, 64, sky)
scene = os.path.join(proj, 'courtyard.scene')
before = os.path.join(root, 'before.png')
mid = os.path.join(root, 'mid.png')
shot = keep_shot or os.path.join(root, 'after.png')
W = lambda n: ['--wait', str(n)]
START = ['--wait-export', '--move', '800,400'] + W(3) + ['--key', 'f'] + W(20) + ['--scroll-to', '@obj.shine'] + W(5) + \
        ['--move', '760,940'] + W(3)


def run(args, png):
    cmd = [editor, '--size', '1600x960', '--project', proj, '--scene', scene, '--select', 'Crate'] + W(30) + START + args + \
          ['--screenshot', png]
    r = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=600)
    lines = (r.stdout + r.stderr).splitlines()
    rects = {}
    for l in lines:
        m = re.match(r'control (\S+(?: \S+)*) at (-?\d+),(-?\d+) (-?\d+),(-?\d+)$', l)
        if m: rects[m.group(1)] = tuple(int(v) for v in m.groups()[1:])
    return r.returncode, rects, [l for l in lines if not l.startswith('control ')]


# First run: where the Shine slot is, and the scene view with no shine.
rc0, rects0, out0 = run([], before)
check('the first run ran to its screenshot', rc0 == 0 and os.path.exists(before), out0[-10:])
slot = rects0.get('obj.shine')
check('the Shine slot is drawn, without strength or blend rows', bool(slot) and 'obj.shinestrength' not in rects0 and
      'obj.shineblend' not in rects0, sorted(k for k in rects0 if k.startswith('obj.shine')))
mats0 = [l for l in out0 if l.startswith('material 0: ')]
check('no shine at first', bool(mats0) and mats0[-1].endswith('shine none'), mats0[-1:])
to = '%d,%d' % ((slot[0] + slot[2]) // 2, (slot[1] + slot[3]) // 2) if slot else '0,0'
# Second run: env.png dropped on the slot, then strength 25 and Half, saved.
rc, rects, out = run(['--click', SEARCH + ',0', '--text', 'env', '--wait', '5', '--drag', TILE + ',' + to + ',0'] + W(10) +
                     ['--move', '760,940'] + W(5) + ['--shot', mid] +
                     ['--scroll-to', '@obj.shinestrength'] + W(3) + ['--dblclick', '@obj.shinestrength'] + W(3) +
                     ['--key', 'ctrl+a', '--text', '25', '--key', 'enter'] + W(5) +
                     ['--click', '@obj.shineblend'] + W(5) + ['--click', '@obj.shineblend/Half'] + W(10) +
                     ['--key', 'ctrl+s'] + W(10) + ['--wait-export'] + W(20) + ['--move', '760,940'] + W(5), shot)

check('the editor ran to its screenshot', rc == 0 and os.path.exists(shot), (rc, out[-10:]))
missing = [l for l in out if 'no such control drawn' in l]
check('every control the script names was drawn', not missing, missing)
mats = [l for l in out if l.startswith('material 0: ')]
check('the drop sets env.png at the default strength and blend',
      'material 0: texture crate.png colour #FFFFFF shine env.png strength 50 blend add' in out, mats)
check('strength and blend take', bool(mats) and mats[-1] == 'material 0: texture crate.png colour #FFFFFF shine env.png strength 25 blend half',
      mats[-3:])
check('the strength and blend rows show once it shines', 'obj.shinestrength' in rects and 'obj.shineblend' in rects,
      sorted(k for k in rects if k.startswith('obj.shine')))
previews = [l for l in out if l.startswith('shine preview: ')]
check('the scene view draws the overlay over the Crate', 'shine preview: 12 triangles' in previews, previews)
view = rects0.get('vp.view')
if view and os.path.exists(before) and os.path.exists(mid):
    # The whole scene view: the Crate is in it (framed with F), and nothing
    # else changes between the two shots.
    b0, b1 = brightness(before, view), brightness(mid, view)
    check('the scene view is brighter with the sky added over the Crate', b1 > b0 + 0.5, (b0, b1))
else:
    check('scene view and shots found', False, (view, os.path.exists(before), os.path.exists(mid)))
saved = json.load(open(scene))


def find(objs, name):
    for o in objs:
        if o['name'] == name: return o
        c = find(o.get('children', []), name)
        if c: return c


crate = find(saved['objects'], 'Crate')
mesh = next(c for c in crate['components'] if c['type'] == 'mesh')
sh = mesh['materials'][0].get('shine')
check('the saved scene has the shine', sh == {'texture': 'textures/env.png', 'strength': 0.25, 'blend': 'half'}, sh)
pack = os.path.join(root, 'out.splashpack')
e = subprocess.run([cli, 'export', scene, '-o', pack], capture_output=True, text=True)
d = open(pack, 'rb').read() if e.returncode == 0 else b''
if len(d) >= 168:
    version, nlua, ngo = struct.unpack_from('<HHH', d, 2)
    table = struct.unpack_from('<I', d, 164)[0]
    count = struct.unpack_from('<H', d, table)[0] if table else 0
    rec = struct.unpack_from('<HH', d, table + 4) + struct.unpack_from('<BB', d, table + 18) if count else None
    check('the export is v30 with one shine record: strength 32, half, 12 Tris',
          version == 30 and count == 1 and rec and rec[1] == 12 and rec[2] == 32 and rec[3] == 1, (version, table, count, rec))
    if rec:
        go = 168 + 8 * nlua + 92 * rec[0]
        polys, pc = struct.unpack_from('<I', d, go)[0], struct.unpack_from('<H', d, go + 52)[0]
        flags = [struct.unpack_from('<H', d, polys + 52 * t + 50)[0] & 4 for t in range(pc)]
        check('the record is the Crate, its Tris flagged 0x4', pc == 12 and all(flags) and
              struct.unpack_from('<i', d, go + 12)[0] == round(1 / 100 * 4096), (pc, flags))
else:
    check('the export is v30 with one shine record: strength 32, half, 12 Tris', False, e.stdout + e.stderr)
print('shinecheck: %s' % ('all passed' if not fails else '%d failed' % fails))
sys.exit(1 if fails else 0)
