# The Game view's Pause, Screenshot, frame rate, overlay placement, crop to
# a safe area and VRAM window (#345), driven through the GUI. Play runs a
# fake pcsx-redux (SPLASHEDIT_REDUX, a shell script) that execs
# emulator_tests (beside the editor) with SPLASHEDIT_FAKE_REDUX=320x240 and
# SPLASHEDIT_FAKE_REDUX_FLIP=6: colour bars, vblanks at about 60 a second
# and the display area moved every 6 of them (10 frames a second), standing
# still while pause.flag is in its console directory, as framecapture.lua
# pauses redux.
#  - The frame rate reads about 10 fps at about 60 Hz; Pause makes it 0 and
#    0, Resume brings it back.
#  - Screenshot writes a 320x240 PNG equal to the console's frame pixel for
#    pixel, with the safe-area guides on in the view.
#  - Cropped to action safe (90%) or title safe (80%), the first colour bar
#    edge moves left by the inset, scaled; uncropped it is where it was.
#  - Overlay in strips: the display bar sits above the picture. Hover: with
#    the pointer away nothing but the picture is drawn.
#  - The VRAM window shows all 1024 x 512 at 1x, live, the frame's pixels at
#    y 0 and at y 256.
#  - The toggles are saved in the play settings (--play-settings).
# usage: python3 gameviewcheck.py <splashedit> [screenshot prefix]   (needs a display: xvfb-run -a)
import os, re, shutil, stat, struct, subprocess, sys, tempfile, zlib

editor = os.path.abspath(sys.argv[1])
display = os.path.join(os.path.dirname(editor), 'emulator_tests')
shots = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else None
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
fails = 0
work = tempfile.mkdtemp()
engine = os.path.join(work, 'psxsplash.ps-exe')
with open(engine, 'wb') as fh:
    fh.write(b'PS-X EXE fake')
FW, FH = 320, 240


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


# The PNG reader safeareacheck.py uses (no third-party modules).
def png_rgb(path):
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


def px(img, x, y):
    w, h, rows = img
    return tuple(rows[y][x * 3:x * 3 + 3])


# The fake's frame (emulator_tests.cpp): 75% bars over the top two thirds,
# black below with a white line every 32 pixels; BGR555 to 8 bits as the
# Game view expands it.
BARS = [0x5ef7, 0x02f7, 0x5ee0, 0x02e0, 0x5c17, 0x0017, 0x5c00, 0x0000]


def rgb555(v):
    c = lambda s: ((v >> s) & 31) << 3 | ((v >> s) & 31) >> 2
    return (c(0), c(5), c(10))


def fake_pixel(x, y):
    if y < FH * 2 // 3:
        return rgb555(BARS[x * 8 // FW])
    return rgb555(0x7fff if x % 32 == 16 else 0)


GREY, YELLOW = rgb555(BARS[0]), rgb555(BARS[1])


def run(name, args, cfg_text=None):
    project = os.path.join(tempfile.mkdtemp(), 'p')
    shutil.copytree(example, project)
    settings = os.path.join(work, name + '.cfg')
    if cfg_text is not None:
        open(settings, 'w').write(cfg_text)
    elif os.path.exists(settings):
        os.remove(settings)
    redux = os.path.join(work, 'fake-redux.sh')
    with open(redux, 'w', newline='\n') as fh:
        fh.write('#!/bin/sh\nSPLASHEDIT_FAKE_REDUX=%dx%d SPLASHEDIT_FAKE_REDUX_FLIP=6 exec "%s" "$@"\n' % (FW, FH, display))
    os.chmod(redux, os.stat(redux).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    env = dict(os.environ, SPLASHEDIT_REDUX=redux, SPLASHEDIT_PSXSPLASH=engine)
    out = os.path.join(shots + '-' + name + '.png') if shots else os.path.join(project, 'shot.png')
    cmd = [editor, '--size', '1600x960', '--project', project, '--scene', os.path.join(project, 'courtyard.scene'),
           '--play-settings', settings, '--wait', '10', '--key', 'f5', '--wait', '240'] + args + ['--screenshot', out]
    r = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=300)
    lines = (r.stdout + r.stderr).strip().splitlines()
    controls = {}
    for l in lines:
        if l.startswith('control ') and ' at ' in l:
            cname, at = l[len('control '):].rsplit(' at ', 1)
            a, b = at.split(' ')
            controls[cname] = tuple(int(v) for v in a.split(',') + b.split(','))
    missing = [l for l in lines if 'no such control drawn' in l]
    check('%s: every control the script names was drawn' % name, not missing, missing)
    cfg = open(settings).read() if os.path.exists(settings) else ''
    return lines, controls, (png_rgb(out) if os.path.exists(out) else None), cfg


def rates(lines):
    """(fps, hz) of each 'game: fps F hz H' line, in order, with the index of the line."""
    out = []
    for i, l in enumerate(lines):
        m = re.match(r'game: fps (-?\d+) hz (-?\d+)$', l)
        if m:
            out.append((i, int(m.group(1)), int(m.group(2))))
    return out


def last_before(rs, index):
    before = [r for r in rs if r[0] < index]
    return before[-1] if before else None


def bar_probes(img, frame):
    """The picture at 25 and 50 screen pixels right of the frame's left, a third of the way down."""
    x0, y0, x1, y1 = frame
    y = y0 + (y1 - y0) // 3
    return px(img, x0 + 25, y), px(img, x0 + 50, y)


# 1. Frame rate, Pause and Resume, Screenshot with the guides on.
lines, controls, img, cfg = run('pause', [
    '--click', '@game.showfps', '--wait', '200',
    '--click', '@game.pause', '--wait', '200',
    '--click', '@game.pause', '--wait', '200',
    '--click', '@game.safearea', '--wait', '5', '--click', '@game.screenshot', '--wait', '20',
    '--click', '@game.safearea', '--move', '1100,880', '--wait', '20'])
for c in ('game.pause', 'game.screenshot', 'game.crop', 'game.showfps', 'game.overlay', 'game.vram', 'game.fps', 'game.frame'):
    check('the Game view draws %s' % c, c in controls, sorted(controls))
rs = rates(lines)
paused_at = next((i for i, l in enumerate(lines) if l == 'game: pause requested'), None)
resumed_at = next((i for i, l in enumerate(lines) if l == 'game: resume requested'), None)
check('Pause and Resume were asked for, in that order', paused_at is not None and resumed_at is not None and paused_at < resumed_at,
      (paused_at, resumed_at))
running = last_before(rs, paused_at or 0)
check('running: about 10 fps at about 60 Hz', running and 7 <= running[1] <= 12 and 45 <= running[2] <= 70, running)
during = last_before(rs, resumed_at or 0)
check('paused: 0 fps and 0 Hz', during and during[1] == 0 and during[2] == 0, during)
check('paused: the view says so', any(l.startswith('game: view') and l.endswith(' paused') for l in lines))
after = rs[-1] if rs else None
check('resumed: the vblanks run again', after and after[0] > (resumed_at or 0) and after[2] >= 45, after)
check('no pause was refused', not any(l == 'game: pause not taken' for l in lines))
shot = next((l for l in lines if l.startswith('game: screenshot ')), '')
m = re.match(r'game: screenshot (\d+)x(\d+) (.*)$', shot)
check('Screenshot says its size and file', m and m.group(1) == str(FW) and m.group(2) == str(FH), shot)
path = m.group(3) if m else ''
check('Screenshot goes to the project\'s screenshots folder as courtyard_001.png',
      path.replace('\\', '/').endswith('/p/screenshots/courtyard_001.png') and os.path.isfile(path), path)
check('the status bar names the file', any(l == 'status: Saved screenshot ' + path for l in lines), path)
if path and os.path.isfile(path):
    sw, sh, srows = png = png_rgb(path)
    check('the screenshot is the console\'s 320x240', (sw, sh) == (FW, FH), (sw, sh))
    bad = [(x, y, px(png, x, y), fake_pixel(x, y)) for y in range(min(sh, FH)) for x in range(min(sw, FW)) if px(png, x, y) != fake_pixel(x, y)]
    check('every screenshot pixel is the frame\'s, no guide or overlay in it', not bad, bad[:5])
check('the frame rate toggle is saved', 'game_fps=1\n' in cfg, cfg)
if img and 'game.frame' in controls:
    check('uncropped: the first bar spans both probes', bar_probes(img, controls['game.frame']) == (GREY, GREY),
          bar_probes(img, controls['game.frame']))

# 2. Cropped to action safe (from the play settings), the overlay only on hover, the pointer away.
lines, controls, img, cfg = run('hover', ['--move', '300,500', '--wait', '30'], 'game_crop=action\ngame_overlay=hover\n')
check('hover, pointer away: the picture is drawn', 'game.frame' in controls, sorted(controls))
check('hover, pointer away: no button is drawn', not any(c in controls for c in ('game.pause', 'game.safearea', 'game.capture', 'game.vram')),
      sorted(c for c in controls if c.startswith('game.')))
check('hover, pointer away: the view says hidden', any(l.startswith('game: view overlay hover hidden crop action') for l in lines))
if img and 'game.frame' in controls:
    # 40 frame pixels of grey, 16 cropped: 24 of 288 across 476 is under 50.
    check('action safe: the first bar ends between the probes', bar_probes(img, controls['game.frame']) == (GREY, YELLOW),
          bar_probes(img, controls['game.frame']))
lines, controls, img, cfg = run('hoveron', ['--move', '1000,400', '--wait', '30'], 'game_overlay=hover\n')
check('hover, pointer on the game: the buttons are drawn', 'game.pause' in controls and 'game.safearea' in controls, sorted(controls))

# 3. Cropped to title safe, the overlay moved into strips by its button.
lines, controls, img, cfg = run('strip', ['--click', '@game.overlay', '--wait', '30', '--move', '1100,880', '--wait', '20'],
                                'game_crop=title\n')
f = controls.get('game.frame')
check('strips: the picture is drawn', f is not None, sorted(controls))
if f:
    for c in ('game.pause', 'game.safearea', 'game.look/RGB', 'game.capture', 'game.vram'):
        r = controls.get(c)
        check('strips: %s sits above the picture' % c, r is not None and r[3] <= f[1], (r, f))
    if img:
        # 8 of 256 grey across 476 is under 25.
        check('title safe: the first bar ends before both probes', bar_probes(img, f) == (YELLOW, YELLOW), bar_probes(img, f))
check('the overlay and crop are saved', 'game_overlay=strip\n' in cfg and 'game_crop=title\n' in cfg, cfg)

# 4. The VRAM window: 1024 x 512 at 1x, live, the frame at y 0 and its copy at y 256.
lines, controls, img, cfg = run('vram', ['--click', '@game.vram', '--wait', '60', '--move', '20,940', '--wait', '10'])
v = controls.get('vram.image')
check('the VRAM window opens at 1x', v is not None and (v[2] - v[0], v[3] - v[1]) == (1024, 512), v)
check('the VRAM window is live', any(l == 'vram: open console 1 scale 1x texture live' for l in lines))
if v and img:
    got = [px(img, v[0] + x, v[1] + y) for x, y in ((5, 5), (45, 100), (5, 261), (45, 356))]
    want = [fake_pixel(5, 5), fake_pixel(45, 100), fake_pixel(5, 5), fake_pixel(45, 100)]
    check('VRAM shows the frame at y 0 and y 256', got == want, (got, want))

print('gameviewcheck: %d failure(s)' % fails if fails else 'gameviewcheck: all passed')
sys.exit(1 if fails else 0)
