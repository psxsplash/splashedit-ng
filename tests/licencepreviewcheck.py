# The Disc section's Preview boot screen, driven through the GUI on a copy of
# the courtyard with a stand-in pcsx-redux (SPLASHEDIT_REDUX: this script
# writes one, as CI has no Sony BIOS to boot). With a licence other than None
# the section has a "Preview boot screen" button; it writes a disc of the
# licence alone (sectors 0-11 the licence, no SYSTEM.CNF) into
# $TMPDIR/splashedit-licence-preview, runs redux on it with the BIOS from Play
# settings (SPLASHEDIT_BIOS) and licencepreview.lua, and shows the screenshot
# redux leaves there (preview.raw, 16-bit 1555 or 24-bit RGB, and
# preview.txt "<w> <h> <bpp>") under the button. Each run prints
# 'disc: licence preview <w>x<h> <status>', status ok, no-bios (none set, or
# OpenBIOS: nothing runs) or failed:<why>.
# usage: python3 licencepreviewcheck.py <splashedit> [<preview.png>]   (needs a display: xvfb-run -a)
import json, os, re, shutil, stat, struct, subprocess, sys, tempfile, zlib

editor = os.path.abspath(sys.argv[1])
shot_out = sys.argv[2] if len(sys.argv) > 2 else None
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
fails = 0
work = tempfile.mkdtemp()
FOLDS = ['--click', '@s_sequences', '--wait', '3', '--click', '@s_rendering', '--wait', '3', '--click', '@s_loading', '--wait', '3',
         '--click', '@s_vram', '--wait', '5']
W, H = 64, 48

# The stand-in redux: records its arguments, then writes a W x H screenshot
# whose quadrants are red, green, blue and yellow (FAKE_MODE 16: 1555 words;
# 24: RGB bytes), or nothing and exits 3 (FAKE_MODE fail).
FAKE = os.path.join(work, 'fake-redux')
open(FAKE, 'w').write('''#!/usr/bin/env python3
import json, os, struct, sys
a = sys.argv[1:]
out = a[a.index('-pcdrvbase') + 1]
json.dump(a, open(os.path.join(out, 'argv.json'), 'w'))
mode = os.environ.get('FAKE_MODE', '16')
if mode == 'fail':
    print('fake redux: no BIOS screen today')
    sys.exit(3)
W, H = %d, %d
def colour(x, y):
    return [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0)][(x >= W // 2) + 2 * (y >= H // 2)]
raw = bytearray()
for y in range(H):
    for x in range(W):
        r, g, b = colour(x, y)
        if mode == '24': raw += bytes((r * 40 // 255 + 10, g * 40 // 255 + 100, b * 40 // 255 + 200))
        else: raw += struct.pack('<H', (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10)
open(os.path.join(out, 'preview.raw'), 'wb').write(raw)
open(os.path.join(out, 'preview.txt'), 'w').write('%%d %%d %%s\\n' %% (W, H, mode))
''' % (W, H))
os.chmod(FAKE, os.stat(FAKE).st_mode | stat.S_IEXEC)
SONY = os.path.join(work, 'sony.bin')
OPEN = os.path.join(work, 'openbios.bin')
open(SONY, 'wb').write(b'\0' * 0x108 + b'Sony Computer Entertainment Inc.' + b'\0' * 1000)
open(OPEN, 'wb').write(b'\0' * 0x108 + b'PS-X Realtime Kernel OpenBios - build id %s.\0OpenBIOS' + b'\0' * 1000)


def expected(x, y, mode):
    r, g, b = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0)][(x >= W // 2) + 2 * (y >= H // 2)]
    if mode == '24': return (r * 40 // 255 + 10, g * 40 // 255 + 100, b * 40 // 255 + 200)
    return tuple((c >> 3) * 255 // 31 for c in (r, g, b))


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def png_rgba(path):
    """(width, height, RGBA bytes) of an 8-bit RGBA PNG."""
    data = open(path, 'rb').read()
    pos, idat = 8, b''
    while pos < len(data):
        n, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
            assert depth == 8 and ctype == 6, path
        elif kind == b'IDAT':
            idat += body
        pos += 12 + n
    raw, stride, out, prev = zlib.decompress(idat), w * 4, bytearray(), bytearray(w * 4)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - 4] if i >= 4 else 0
            b, c = prev[i], prev[i - 4] if i >= 4 else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out += line
        prev = line
    return w, h, bytes(out)


def write_png_rgba(path, w, h, px):
    chunk = lambda kind, body: struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    rows = b''.join(b'\0' + px[y * w * 4:(y + 1) * w * 4] for y in range(h))
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def run(args, bios=SONY, mode='16', licence=None):
    """Runs the editor on a fresh courtyard copy (with disc.json's licence
    set to `licence` when given) and its own TMPDIR, folds the sections above
    Disc, then `args` and a screenshot. Returns the printed disc: lines, the
    named controls' rects, the screenshot and the preview directory."""
    project = os.path.join(tempfile.mkdtemp(), 'courtyard')
    shutil.copytree(example, project)
    if licence is not None:
        json.dump({'licence': licence}, open(os.path.join(project, 'disc.json'), 'w'))
    tmp = tempfile.mkdtemp()
    env = dict(os.environ, SPLASHEDIT_REDUX=FAKE, SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe', TMPDIR=tmp,
               FAKE_MODE=mode)
    env.pop('SPLASHEDIT_BIOS', None)
    if bios: env['SPLASHEDIT_BIOS'] = bios
    shot = os.path.join(project, 'end.png')
    cmd = [editor, '--size', '1600x960', '--project', project, '--wait', '5'] + FOLDS + args + ['--wait', '5', '--screenshot', shot]
    r = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=180)
    lines = (r.stdout + r.stderr).strip().splitlines()
    missing = [l for l in lines if 'no such control drawn' in l]
    check('every control the script names was drawn', not missing, missing)
    rects = {}
    for l in lines:
        m = re.match(r'control (\S+(?: \S+)*) at (-?\d+),(-?\d+) (-?\d+),(-?\d+)$', l)
        if m: rects[m.group(1)] = tuple(int(v) for v in m.groups()[1:])
    return [l for l in lines if l.startswith('disc:')], rects, shot, os.path.join(tmp, 'splashedit-licence-preview')


PREVIEW = ['--scroll-to', '@disc.licence.preview', '--wait', '3', '--click', '@disc.licence.preview', '--wait', '3', '--wait-play',
           '--wait', '5']
SHOW = ['--scroll-to', '@disc.licence.previewimage', '--wait', '5']


def previews(discs):
    return [l for l in discs if l.startswith('disc: licence preview ')]


def shown(shot, rect, mode):
    """Pixels of the drawn preview, one per quadrant (a quarter in from its
    corners), against what the stand-in wrote; and the crop itself."""
    w, h, px = png_rgba(shot)
    x0, y0, x1, y1 = rect
    got, want = [], []
    for qx in (0, 1):
        for qy in (0, 1):
            fx, fy = (qx * 2 + 1) / 4, (qy * 2 + 1) / 4
            sx, sy = int(x0 + fx * (x1 - x0)), int(y0 + fy * (y1 - y0))
            got.append(tuple(px[(sy * w + sx) * 4:(sy * w + sx) * 4 + 3]))
            want.append(expected(int(fx * W), int(fy * H), mode))
    crop = b''.join(px[(y * w + x0) * 4:(y * w + x1) * 4] for y in range(y0, y1))
    return got, want, (x1 - x0, y1 - y0, crop)


# The default (psxsplash) licence: the button, then the frame under it.
discs, rects, shot, pdir = run(PREVIEW + SHOW)
check('the preview reports ok at the stand-in\'s size', previews(discs) == ['disc: licence preview %dx%d ok' % (W, H)], previews(discs))
argv = json.load(open(os.path.join(pdir, 'argv.json'))) if os.path.exists(os.path.join(pdir, 'argv.json')) else []
cue = os.path.join(pdir, 'licence.cue')
check('redux runs with no window, the BIOS and the licence disc',
      all(a in argv for a in ('-no-ui', '-no-fastboot', '-safe')) and argv[argv.index('-bios') + 1:][:1] == [SONY] and
      argv[argv.index('-iso') + 1:][:1] == [cue] and argv[-2:-1] == ['-dofile'] and argv[-1].endswith('licencepreview.lua'), argv)
binp = os.path.join(pdir, 'licence.bin')
bin_ = open(binp, 'rb').read() if os.path.exists(binp) else b''
check('the disc holds the licence (Created with psxsplash at LBA 4)', b'Created wit' in bin_[4 * 2352 + 24:4 * 2352 + 24 + 64],
      bin_[4 * 2352 + 24:4 * 2352 + 24 + 64])
check('the disc has no SYSTEM.CNF', bool(bin_) and b'SYSTEM.CNF' not in bin_, len(bin_))
r = rects.get('disc.licence.previewimage')
check('the frame is drawn under the button', bool(r) and bool(rects.get('disc.licence.preview')) and
      r[1] > rects['disc.licence.preview'][3], (r, rects.get('disc.licence.preview')))
if r:
    got, want, crop = shown(shot, r, '16')
    check('the frame shows the 16-bit screenshot\'s colours', got == want, (got, want))
    check('the frame keeps the screenshot\'s proportions', abs((r[2] - r[0]) * H - (r[3] - r[1]) * W) <= W, r)
    if shot_out: write_png_rgba(shot_out, *crop)
dec = os.path.join(pdir, 'preview.png')
if os.path.exists(dec):
    w, h, px = png_rgba(dec)
    check('preview.png is the decoded screenshot', (w, h) == (W, H) and all(
        tuple(px[(y * w + x) * 4:(y * w + x) * 4 + 3]) == expected(x, y, '16') for y in range(h) for x in range(w)), (w, h))
else:
    check('preview.png is written', False, dec)

# 24-bit RGB.
discs, rects, shot, pdir = run(PREVIEW + SHOW, mode='24')
check('a 24-bit screenshot reports ok', previews(discs) == ['disc: licence preview %dx%d ok' % (W, H)], previews(discs))
if rects.get('disc.licence.previewimage'):
    got, want, _ = shown(shot, rects['disc.licence.previewimage'], '24')
    check('the frame shows the 24-bit screenshot\'s colours', got == want, (got, want))
else:
    check('the 24-bit frame is drawn', False, sorted(n for n in rects if n.startswith('disc.licence')))

# No BIOS set, or OpenBIOS: redux does not run and the section says why.
for name, bios in (('no BIOS', None), ('OpenBIOS', OPEN)):
    discs, rects, shot, pdir = run(PREVIEW, bios=bios)
    check(name + ': the preview reports no-bios', previews(discs) == ['disc: licence preview 0x0 no-bios'], previews(discs))
    check(name + ': redux is not run', not os.path.exists(os.path.join(pdir, 'argv.json')), pdir)
    check(name + ': the status line is drawn, no frame', 'disc.licence.previewstatus' in rects and
          'disc.licence.previewimage' not in rects, sorted(n for n in rects if n.startswith('disc.licence')))

# A redux that exits without a screenshot.
discs, rects, shot, pdir = run(PREVIEW, mode='fail')
check('a redux that writes nothing reports failed:exit-3', previews(discs) == ['disc: licence preview 0x0 failed:exit-3'],
      previews(discs))
check('no frame after a failure', 'disc.licence.previewimage' not in rects, sorted(n for n in rects if n.startswith('disc.licence')))

# Licence None: no button.
discs, rects, shot, pdir = run([], licence='none')
check('no preview button with licence None', 'disc.licence.preview' not in rects and 'disc.licence' in rects,
      sorted(n for n in rects if n.startswith('disc.licence')))

shutil.rmtree(work, ignore_errors=True)
print('%d failure%s' % (fails, '' if fails == 1 else 's'))
sys.exit(1 if fails else 0)
