# The licence builder's logo from a model, driven through the GUI on a copy of
# the courtyard: Licence > Build..., Choose file... answered with an OBJ (an
# L, tests/fixtures/licence-logo/L.obj) converts it to a flat-triangle TMD
# (splash::modelToLicenceTmd) and the status line reads '<name> converted: N
# triangles, B bytes'; a Logo size row (disc.licence.logosize) shows for a
# model and converts it again at the size typed. Build writes the licence, and
# the simulated boot screen (no BIOS) draws the L upright and not mirrored in
# the stock logo's box.
# usage: python3 licencetmdcheck.py <splashedit> [<shot.png>]   (needs a display: xvfb-run -a)
import os, re, shutil, struct, subprocess, sys, tempfile, zlib

editor = os.path.abspath(sys.argv[1])
shot_out = sys.argv[2] if len(sys.argv) > 2 else None
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
model = os.path.join(here, 'fixtures', 'licence-logo', 'L.obj')
fails = 0
FOLDS = ['--click', '@s_sequences', '--wait', '3', '--click', '@s_rendering', '--wait', '3', '--click', '@s_loading', '--wait', '3',
         '--click', '@s_vram', '--wait', '5']
OPEN = ['--click', '@disc.licence', '--wait', '3', '--click', '@disc.licence/Build...', '--wait', '5',
        '--scroll-to', '@disc.licence.cancel', '--wait', '3', '--click', '@disc.licence.tmd', '--wait', '5']
BUILD = ['--scroll-to', '@disc.licence.build', '--wait', '3', '--click', '@disc.licence.build', '--wait', '5']
PREVIEW = ['--scroll-to', '@disc.licence.preview', '--wait', '3', '--click', '@disc.licence.preview', '--wait', '3', '--wait-play',
           '--wait', '5', '--scroll-to', '@disc.licence.previewimage', '--wait', '5']
CONVERTED = 'L.obj converted: 24 triangles, 584 bytes'


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


def run(args):
    """Runs the editor on a fresh courtyard copy with every open-file dialog
    answered with the L, folds the sections above Disc, then `args` and a
    screenshot. Returns the project, the disc: lines, the named controls'
    rects and the screenshot."""
    project = os.path.join(tempfile.mkdtemp(), 'courtyard')
    shutil.copytree(example, project)
    env = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe',
               SPLASHEDIT_FILE_DIALOG='pick:' + model, TMPDIR=tempfile.mkdtemp())
    env.pop('SPLASHEDIT_BIOS', None)
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
    return project, [l for l in lines if l.startswith('disc:') or l.startswith('file dialog:')], rects, shot


def licence_tmd(project):
    """The logo TMD's vertices in <project>/custom-licence.dat (sectors 5-11)."""
    p = os.path.join(project, 'custom-licence.dat')
    if not os.path.exists(p): return None
    d = open(p, 'rb').read()
    t = b''.join(d[i * 2336 + 8:i * 2336 + 8 + 2048] for i in range(5, 12))
    vert_top, n_vert = struct.unpack('<2I', t[12:20])
    return [struct.unpack('<3h', t[12 + vert_top + i * 8:12 + vert_top + i * 8 + 6]) for i in range(n_vert)]


# Pick, Build, preview.
project, lines, rects, shot = run(OPEN + BUILD + PREVIEW)
check('Choose file... is answered with the model', any(l.startswith('file dialog: licencetmd picked ') and l.endswith('L.obj')
                                                       for l in lines), lines)
builds = [l for l in lines if l.startswith('disc: licence build ')]
check('the status line reports the conversion', any(l.endswith(' ' + CONVERTED) for l in builds), builds[-2:])
verts = licence_tmd(project)
check('Build writes custom-licence.dat with the converted logo', verts is not None and len(verts) == 14, verts and len(verts))  # 16 corners, 2 shared
check('the preview is simulated', 'disc: licence preview 640x480 simulated' in lines, [l for l in lines if 'preview' in l])
r = rects.get('disc.licence.previewimage')
check('the preview frame is drawn', bool(r), sorted(n for n in rects if n.startswith('disc.licence')))
if r:
    w, h, px = png_rgba(shot)
    x0, y0, x1, y1 = r

    def lit(fx0, fy0, fx1, fy1):
        """Lit screenshot pixels over the frame's 640x480 points [fx0, fx1) x [fy0, fy1)."""
        n = 0
        for fy in range(fy0, fy1, 2):
            sy = int(y0 + (fy + 0.5) * (y1 - y0) / 480)
            for fx in range(fx0, fx1, 2):
                sx = int(x0 + (fx + 0.5) * (x1 - x0) / 640)
                n += any(px[(sy * w + sx) * 4 + k] > 24 for k in range(3))
        return n
    # The stock logo's box x 214..422, y 65..263; its centre 318, 164.
    # Outside: above it and beside it (the wordmark and text are below it), off the frame's edge.
    inside, outside = lit(214, 65, 423, 264), lit(8, 8, 632, 60) + lit(8, 60, 200, 270) + lit(440, 60, 632, 270)
    check('the logo is drawn inside the stock logo box', inside > 1000 and outside == 0, (inside, outside))
    br, tr, tl = lit(318, 164, 423, 264), lit(318, 65, 423, 164), lit(214, 65, 318, 164)
    check('the L is upright (foot at the bottom right)', br > tr, (br, tr))
    check('the L is not mirrored (bar on the left)', tl > tr, (tl, tr))
    if shot_out: shutil.copyfile(shot, shot_out)

# Logo size 50: the row shows for a model (the script's double-click on it
# finds it drawn) and converts it again, half the size.
project, lines, rects, shot = run(OPEN + ['--dblclick', '@disc.licence.logosize', '--wait', '3', '--key', 'ctrl+a', '--text', '50',
                                          '--key', 'enter', '--wait', '3'] + BUILD)
small = licence_tmd(project)
if verts and small:
    big, half = max(abs(c) for v in verts for c in v), max(abs(c) for v in small for c in v)
    check('Logo size 50 builds a logo about half the size', 0.4 * big < half < 0.6 * big, (big, half))
else:
    check('Logo size 50 builds a licence', False, small)

print('%d failure%s' % (fails, '' if fails == 1 else 's'))
sys.exit(1 if fails else 0)
