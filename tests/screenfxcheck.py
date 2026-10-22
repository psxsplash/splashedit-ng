# Screen effect tracks (psxsplash TrackType 19-23) end to end in the editor.
# Export: cutscene/cutscene.scene with a cutscene holding one of each must pack
# them as types 19-23 with no target and the encodings docs/splashpack-format.md
# gives; an animation with one fails the export; resave keeps the JSON names.
# Timeline (GUI, a copy of the courtyard example): the Add track menu offers
# the five, and picking Motion Trail adds a trail track; scrubbing a cutscene
# with a fade to red, a letterbox and a trail prints "screenfx: preview ..."
# lines that follow the keys, and the Scene view shows the bars (black rows at
# the top and bottom of the view) and the red, none of which is saved.
# usage: python3 screenfxcheck.py <splashedit> [<splashpack-cli>] [--shot <png>]   (needs a display: xvfb-run -a)
import json, os, shutil, struct, subprocess, sys, tempfile, zlib

args = sys.argv[1:]
shot = None
if '--shot' in args:
    i = args.index('--shot')
    shot = os.path.abspath(args[i + 1])
    del args[i:i + 2]
editor = os.path.abspath(args[0])
cli = os.path.abspath(args[1]) if len(args) > 1 else os.path.join(os.path.dirname(editor), 'cli', 'splashpack-cli')
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
tmp = tempfile.mkdtemp()
fails = 0
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def key(frame, value, interp=None):
    k = {'frame': frame, 'value': value}
    if interp:
        k['interp'] = interp
    return k


FX_TRACKS = [
    {'type': 'screenFade', 'target': '', 'keyframes': [key(0, [0, 0, 0]), key(60, [1, 0, 0])]},
    {'type': 'screenFadeColor', 'target': '', 'keyframes': [key(0, [1, 0.5, 0])]},
    {'type': 'letterbox', 'target': '', 'keyframes': [key(0, [0, 0, 0]), key(30, [300, 0, 0])]},
    {'type': 'shake', 'target': '', 'keyframes': [key(10, [100, 0, 0])]},
    {'type': 'trail', 'target': '', 'keyframes': [key(5, [0.7, 0, 0]), key(40, [0, 0, 0])]},
]

# ---- Export -------------------------------------------------------------

base = json.load(open(os.path.join(here, 'cutscene', 'cutscene.scene')))
for o in base['objects']:
    for c in o['components']:
        if c['type'] == 'mesh':
            c['mesh'] = os.path.join(here, 'cutscene', c['mesh'])
            for m in c['materials']:
                m['texture'] = os.path.join(here, 'cutscene', m['texture'])
base['settings']['script'] = os.path.join(here, 'cutscene', base['settings']['script'])


def export(edit):
    s = json.loads(json.dumps(base))
    edit(s)
    path = os.path.join(tmp, 'fx.scene')
    json.dump(s, open(path, 'w'))
    out = os.path.join(tmp, 'fx.splashpack')
    r = subprocess.run([cli, 'export', path, '-o', out], capture_output=True, text=True)
    return path, r, (open(out, 'rb').read() if r.returncode == 0 else b'')


def cutscenes(d):
    """{name: [(type, target, [(frame, v0, v1, v2)])]} as psxsplash's splashpack.cpp reads them."""
    n = struct.unpack_from('<H', d, 84)[0]
    table = struct.unpack_from('<I', d, 88)[0]
    out = {}
    for i in range(n):
        doff, _, _, _, _, noff = struct.unpack_from('<IBBBBI', d, table + i * 12)
        _, ntr, _, troff, _ = struct.unpack_from('<HBBII', d, doff)
        tracks = []
        for t in range(ntr):
            typ, nk, tnl, _, tnoff, kfoff = struct.unpack_from('<BBBBII', d, troff + t * 12)
            keys = [struct.unpack_from('<Hhhh', d, kfoff + k * 8) for k in range(nk)]
            tracks.append((typ, tnl, [(k[0] & 0x1FFF, k[1], k[2], k[3]) for k in keys]))
        out[d[noff:d.index(b'\0', noff)].decode()] = tracks
    return out


def add_fx(s):
    s['cutscenes'].append({'name': 'fx', 'durationFrames': 90, 'audioEvents': [], 'tracks': FX_TRACKS})


path, r, d = export(add_fx)
check('a cutscene with every screen track exports', r.returncode == 0, r.stderr[-300:])
fx = cutscenes(d).get('fx', []) if d else []
check('the tracks are types 19-23 with no target name', [(t, n) for t, n, _ in fx] == [(19, 0), (20, 0), (21, 0), (22, 0), (23, 0)],
      [(t, n) for t, n, _ in fx])
if len(fx) == 5:
    check('fade: 0..1 of the colour as 0..255', fx[0][2] == [(0, 0, 0, 0), (60, 255, 0, 0)], fx[0][2])
    check('fade colour: 0..1 RGB as 0..255', fx[1][2] == [(0, 255, 128, 0)], fx[1][2])
    check('letterbox: pixels, at most half the 240 lines', fx[2][2] == [(0, 0, 0, 0), (30, 120, 0, 0)], fx[2][2])
    check('shake: pixels, at most 64', fx[3][2] == [(10, 64, 0, 0)], fx[3][2])
    check('trail: on/off', fx[4][2] == [(5, 1, 0, 0), (40, 0, 0, 0)], fx[4][2])
r2 = subprocess.run([cli, 'resave', path, os.path.join(tmp, 'again.scene')], capture_output=True, text=True)
again = json.load(open(os.path.join(tmp, 'again.scene'))) if r2.returncode == 0 else {}
names = [t['type'] for c in again.get('cutscenes', []) if c['name'] == 'fx' for t in c['tracks']]
check('resave keeps the track names', names == ['screenFade', 'screenFadeColor', 'letterbox', 'shake', 'trail'], (names, r2.stderr))


def anim_fx(s):
    s.setdefault('animations', []).append({'name': 'wobble', 'durationFrames': 30, 'audioEvents': [],
                                           'tracks': [{'type': 'shake', 'target': '', 'keyframes': [key(0, [4, 0, 0])]}]})


_, r, _ = export(anim_fx)
check('an animation with a screen track fails the export', r.returncode != 0 and
      'screen effect tracks only play in cutscenes' in r.stderr, r.stderr[-300:])

# ---- Timeline (GUI) -----------------------------------------------------

# Layout at 1600x960 with the timeline open (as timelinepreviewcheck.py): frame
# f of a 150-frame cutscene on the ruler at x 532 + f * 4.4267.
def fx_x(frame):
    return round(532 + frame * (1224 - 532 - 28) / 150)


RULER_Y = 668


def project_copy(tracks):
    project = os.path.join(tempfile.mkdtemp(), 'courtyard')
    shutil.copytree(example, project)
    scene = os.path.join(project, 'courtyard.scene')
    s = json.load(open(scene, encoding='utf-8'))
    s['cutscenes'] = [{'name': 'intro', 'durationFrames': 150, 'audioEvents': [], 'tracks': tracks}]
    json.dump(s, open(scene, 'w', encoding='utf-8'), indent=2)
    return project


out = ''


def run(tracks, args, png=None, save=False):
    """The editor on a fresh copy; returns the saved scene (when `save`), the
    named controls as last drawn and the screenshot; `out` gets the output."""
    global out
    project = project_copy(tracks)
    png = png or os.path.join(project, 'shot.png')
    cmd = [editor, '--size', '1600x960', '--project', project] + args
    if save:
        cmd += ['--key', 'ctrl+s', '--wait', '10']
    cmd += ['--screenshot', png]
    r = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=180)
    out = (r.stdout + r.stderr).strip()
    lines = out.splitlines()
    missing = [l for l in lines if 'no such control drawn' in l]
    check('every control the run names was drawn', not missing, missing)
    controls = {}
    for l in lines:
        if l.startswith('control '):
            name, rest = l[len('control '):].rsplit(' at ', 1)
            a, b = rest.split(' ')
            x0, y0 = map(int, a.split(','))
            x1, y1 = map(int, b.split(','))
            controls[name] = (x0, y0, x1, y1)
    saved = json.load(open(os.path.join(project, 'courtyard.scene'), encoding='utf-8'))
    return saved, controls, png


def read_png(path):
    """RGB rows of an 8-bit RGB/RGBA PNG (no interlace)."""
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    pos, idat = 8, []
    while pos < len(data):
        n, typ = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if typ == b'IHDR':
            w, h, depth, ctype, _, _, interlace = struct.unpack('>IIBBBBB', body)
            assert depth == 8 and ctype in (2, 6) and not interlace, (depth, ctype, interlace)
            bpp = 3 if ctype == 2 else 4
        elif typ == b'IDAT':
            idat.append(body)
        pos += 12 + n
    raw = zlib.decompress(b''.join(idat))
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append([tuple(line[x * bpp:x * bpp + 3]) for x in range(w)])
        prev = line
    return rows


def preview_lines():
    return [l[len('screenfx: preview '):] for l in out.splitlines() if l.startswith('screenfx: preview ')]


SCRUB_TRACKS = [
    {'type': 'screenFade', 'target': '', 'keyframes': [key(0, [0, 0, 0]), key(100, [1, 0, 0])]},
    {'type': 'screenFadeColor', 'target': '', 'keyframes': [key(0, [1, 0, 0])]},
    {'type': 'letterbox', 'target': '', 'keyframes': [key(0, [0, 0, 0]), key(50, [40, 0, 0])]},
    {'type': 'trail', 'target': '', 'keyframes': [key(60, [1, 0, 0])]},
]


def scrub(frame):
    return ['--timeline', 'intro', '--click', f'{fx_x(frame)},{RULER_Y}', '--wait', '3']


# Frame 50: half the bars' 40 lines... all of them, half the fade, no trail yet.
saved, c, png50 = run(SCRUB_TRACKS, scrub(50), save=True)
lines = preview_lines()
check('scrubbing to 50 prints the screen preview', lines and lines[-1] == 'fade 50% #FF0000 letterbox 40 shake 0 trail off', lines[-3:])
# The same save without the scrub: the scrub must change nothing in the file (a save adds the
# default interp to every key, so the authored list is not the reference).
unscrubbed, _, _ = run(SCRUB_TRACKS, ['--timeline', 'intro', '--wait', '3'], save=True)
check('the preview is not saved', saved == unscrubbed, json.dumps(saved.get('cutscenes'))[:400])
view = c.get('screenfx.preview')
check('the Scene view carries the overlay', view is not None, sorted(k for k in c if k.startswith('screenfx')))
if view:
    rows = read_png(png50)
    x0, y0, x1, y1 = view
    bar = round(40 * (y1 - y0) / 240)
    mid = (x0 + x1) // 2
    top = [rows[y][mid] for y in range(y0 + 2, y0 + bar - 2)]
    bottom = [rows[y][mid] for y in range(y1 - bar + 2, y1 - 2)]
    check('black letterbox bars over the top and the bottom of the view',
          top and bottom and all(max(p) < 16 for p in top + bottom), (bar, top[:3], bottom[:3]))
    inside = [rows[y][x] for y in range(y0 + bar + 30, y1 - bar - 30, 17) for x in range(x0 + 20, x1 - 20, 23)]
    reddish = sum(1 for p in inside if p[0] > p[1] + 40 and p[0] > p[2] + 40)
    check('the view between them is tinted red', inside and reddish > 0.8 * len(inside), (reddish, len(inside)))

# Frame 75: the trail is on, the fade three quarters of the way.
run(SCRUB_TRACKS, scrub(75), png=shot)
lines = preview_lines()
check('scrubbing to 75 follows the keys', lines and lines[-1] == 'fade 75% #FF0000 letterbox 40 shake 0 trail on', lines[-3:])
# Frame 0: nothing to show.
run(SCRUB_TRACKS, scrub(0))
lines = preview_lines()
check('at frame 0 the screen shows clear', lines and lines[-1] == 'fade 0% #FF0000 letterbox 0 shake 0 trail off', lines[-3:])

# The Add track menu offers the five screen tracks; Motion Trail adds one.
_, c, _ = run([], ['--timeline', 'intro', '--wait', '3', '--click', '@addtrack', '--wait', '3'])
want = ['Screen Fade', 'Fade Color', 'Letterbox', 'Screen Shake', 'Motion Trail']
check('Add track offers the screen tracks', all('timeline.addtrack/' + n in c for n in want),
      sorted(k for k in c if k.startswith('timeline.addtrack/')))
saved, _, _ = run([], ['--timeline', 'intro', '--wait', '3', '--click', '@addtrack', '--wait', '3',
                       '--click', '@timeline.addtrack/Motion Trail', '--wait', '3'], save=True)
check('picking Motion Trail adds a trail track', [t['type'] for t in saved['cutscenes'][0]['tracks']] == ['trail'],
      saved['cutscenes'][0]['tracks'])

print(f'{fails} failures' if fails else 'all passed')
sys.exit(1 if fails else 0)
