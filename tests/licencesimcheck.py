# The Disc section's simulated boot screen, driven through the GUI on a copy
# of the courtyard. With no BIOS in Play settings, Preview boot screen does not
# boot anything: it re-renders the licence screen in software
# (splash::simulateLicence: the logo TMD through the BIOS shell's GTE steps,
# the text in the editor's font) and shows the 640x480 frame under the
# button, printing 'disc: licence preview 640x480 simulated'. The Simulated
# toggle (disc.licence.previewsim) asks for the same with a BIOS set. A
# simulated frame is redrawn when the licence file changes.
# usage: python3 licencesimcheck.py <splashedit> [<splashpack-cli> [<shot.png>]]   (needs a display: xvfb-run -a)
# (<shot.png>: the first run's screenshot, the Disc section with the simulated frame)
import json, os, re, shutil, stat, struct, subprocess, sys, tempfile, zlib

editor = os.path.abspath(sys.argv[1])
cli = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else os.path.join(os.path.dirname(editor), 'cli', 'splashpack-cli')
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
fails = 0
work = tempfile.mkdtemp()
FOLDS = ['--click', '@s_sequences', '--wait', '3', '--click', '@s_rendering', '--wait', '3', '--click', '@s_loading', '--wait', '3',
         '--click', '@s_vram', '--wait', '5']
SIM = 'disc: licence preview 640x480 simulated'

# A stand-in redux that only records that it ran (a simulated preview runs nothing).
FAKE = os.path.join(work, 'fake-redux')
open(FAKE, 'w').write('''#!/usr/bin/env python3
import json, os, sys
a = sys.argv[1:]
json.dump(a, open(os.path.join(a[a.index('-pcdrvbase') + 1], 'argv.json'), 'w'))
sys.exit(3)
''')
os.chmod(FAKE, os.stat(FAKE).st_mode | stat.S_IEXEC)
SONY = os.path.join(work, 'sony.bin')
open(SONY, 'wb').write(b'\0' * 0x108 + b'Sony Computer Entertainment Inc.' + b'\0' * 1000)


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def png_rgba(path):
    """(width, height, RGBA bytes) of an 8-bit RGBA PNG (zlib + struct: CI has no PIL)."""
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


def run(args, bios=None, licence=None, files=()):
    """Runs the editor on a fresh courtyard copy (disc.json's licence set to
    `licence` when given, `files` (from, name) copied in) with its own TMPDIR, folds the
    sections above Disc, then `args` and a screenshot. Returns the printed
    disc: lines, the named controls' rects, the screenshot, the project and
    the preview directory."""
    project = os.path.join(tempfile.mkdtemp(), 'courtyard')
    shutil.copytree(example, project)
    for src, name in files: shutil.copy(src, os.path.join(project, name))
    if licence is not None:
        json.dump({'licence': licence}, open(os.path.join(project, 'disc.json'), 'w'))
    tmp = tempfile.mkdtemp()
    env = dict(os.environ, SPLASHEDIT_REDUX=FAKE, SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe', TMPDIR=tmp)
    env.pop('SPLASHEDIT_BIOS', None)
    if bios: env['SPLASHEDIT_BIOS'] = bios
    shot = os.path.join(project, 'end.png')
    cmd = [editor, '--size', '1600x960', '--project', project, '--wait', '5'] + FOLDS + \
          [a.replace('{project}', project) for a in args] + ['--wait', '5', '--screenshot', shot]
    r = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=180)
    lines = (r.stdout + r.stderr).strip().splitlines()
    missing = [l for l in lines if 'no such control drawn' in l]
    check('every control the script names was drawn', not missing, missing)
    rects = {}
    for l in lines:
        m = re.match(r'control (\S+(?: \S+)*) at (-?\d+),(-?\d+) (-?\d+),(-?\d+)$', l)
        if m: rects[m.group(1)] = tuple(int(v) for v in m.groups()[1:])
    return [l for l in lines if l.startswith('disc:')], rects, shot, project, os.path.join(tmp, 'splashedit-licence-preview')


PREVIEW = ['--scroll-to', '@disc.licence.preview', '--wait', '3', '--click', '@disc.licence.preview', '--wait', '3', '--wait-play',
           '--wait', '5']
SHOW = ['--scroll-to', '@disc.licence.previewimage', '--wait', '5']


def previews(discs):
    return [l for l in discs if l.startswith('disc: licence preview ')]


def at(shot, rect, fx, fy):
    """The screenshot's pixel at frame point (fx, fy) of a 640x480 preview drawn in `rect`."""
    w, h, px = png_rgba(shot)
    x0, y0, x1, y1 = rect
    sx, sy = int(x0 + (fx + 0.5) * (x1 - x0) / 640), int(y0 + (fy + 0.5) * (y1 - y0) / 480)
    return tuple(px[(sy * w + sx) * 4:(sy * w + sx) * 4 + 3])


def crop(shot, rect):
    w, h, px = png_rgba(shot)
    x0, y0, x1, y1 = rect
    return b''.join(px[(y * w + x0) * 4:(y * w + x1) * 4] for y in range(y0, y1))


# No BIOS: the default (psxsplash) licence simulated, nothing run.
discs, rects, shot, project, pdir = run(PREVIEW + SHOW)
check('no BIOS: the preview reports simulated at 640x480', previews(discs) == [SIM], previews(discs))
check('no BIOS: redux is not run', not os.path.exists(os.path.join(pdir, 'argv.json')), pdir)
check('the Simulated toggle is drawn beside the button', 'disc.licence.previewsim' in rects and 'disc.licence.preview' in rects and
      rects['disc.licence.previewsim'][0] > rects['disc.licence.preview'][2], (rects.get('disc.licence.previewsim'), rects.get('disc.licence.preview')))
r = rects.get('disc.licence.previewimage')
check('the frame and its status line are drawn under the button', bool(r) and 'disc.licence.previewstatus' in rects and
      r[1] > rects['disc.licence.preview'][3], sorted(n for n in rects if n.startswith('disc.licence')))
if r:
    check('the frame keeps 640x480 proportions', abs((r[2] - r[0]) * 480 - (r[3] - r[1]) * 640) <= 640, r)
    red = at(shot, r, 320, 150)
    check('the logo\'s P is drawn, lit red', red[0] > 200 and red[1] < 60 and red[2] < 60, red)
    corners = [at(shot, r, x, y) for x, y in ((20, 20), (620, 20), (20, 460), (620, 460))]
    check('the background is black', all(c == (0, 0, 0) for c in corners), corners)
    if len(sys.argv) > 3: open(sys.argv[3], 'wb').write(open(shot, 'rb').read())

# A BIOS set and the toggle on: simulated too, nothing run.
discs, rects, shot, project, pdir = run(['--scroll-to', '@disc.licence.preview', '--wait', '3', '--click', '@disc.licence.previewsim',
                                         '--wait', '3'] + PREVIEW + SHOW, bios=SONY)
check('Simulated on with a BIOS: the preview reports simulated', previews(discs) == [SIM], previews(discs))
check('Simulated on with a BIOS: redux is not run', not os.path.exists(os.path.join(pdir, 'argv.json')), pdir)
check('Simulated on with a BIOS: the frame is drawn', 'disc.licence.previewimage' in rects, sorted(rects))

# The licence file changes under the editor: the frame is redrawn (a second
# report), and differs (a western layout: chars 64+ and the SCEA line).
a, b = os.path.join(work, 'a.lic'), os.path.join(work, 'b.lic')
built = subprocess.run([cli, 'licence', '--out', a], capture_output=True, text=True).returncode == 0 and \
    subprocess.run([cli, 'licence', '--out', b, '--region', 'america', '--top', '          Licensed  by',
                    '--bottom', 'Sony Computer Entertainment Amer  ica '], capture_output=True, text=True).returncode == 0
check('splashpack-cli builds the two licences', built, cli)
if built:
    first = os.path.join(work, 'first.png')
    discs, rects, shot, project, pdir = run(PREVIEW + SHOW + ['--shot', first, '--wait', '5', '--copy-file', b + '@{project}/lic.dat',
                                            '--wait', '120'], licence='lic.dat', files=[(a, 'lic.dat')])
    check('a changed licence file redraws the simulated frame', previews(discs) == [SIM, SIM], previews(discs))
    r = rects.get('disc.licence.previewimage')
    check('the redrawn frame differs from the first', bool(r) and os.path.exists(first) and crop(first, r) != crop(shot, r), r)

shutil.rmtree(work, ignore_errors=True)
print('%d failure%s' % (fails, '' if fails == 1 else 's'))
sys.exit(1 if fails else 0)
