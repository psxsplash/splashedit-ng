# The Game view and Play's Console out in OS windows of their own (#345),
# driven through the GUI. Play runs the fake pcsx-redux gamedockcheck.py uses
# (emulator_tests with SPLASHEDIT_FAKE_REDUX=320x240: colour bars).
#  - View > Game View in Own Window while a game runs opens a second window
#    with the game's picture in it (not blank: the bars), and the main
#    window's tab takes the room the split had.
#  - The keyboard is the controller in that window while it has the focus,
#    and not once the main window has it.
#  - Closing the window (its close button) or its Attach puts the Game view
#    back: the main window's controls are where they were before it left.
#  - Out or not, and where the window was, are saved in the play settings
#    (--play-settings); saved out, the window opens there at the next Play.
#  - The Console's Own window does the same for Play's output.
# Detached windows are written beside each screenshot as <shot>-game.png and
# <shot>-console.png.
# usage: python3 detachcheck.py <splashedit> [screenshot prefix]   (needs a display: xvfb-run -a)
import os, shutil, stat, struct, subprocess, sys, tempfile, zlib

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
DETACH_GAME = ['--click', '@menu.view', '--wait', '3', '--click', '@menu.view/Game View in Own Window', '--wait', '30']


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


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


def colours(path, rect=None, step=3):
    """The set of colours in `path` (inside rect x0,y0,x1,y1 when given), every `step` pixels."""
    w, h, rows = png_rgb(path)
    x0, y0, x1, y1 = rect if rect else (0, 0, w, h)
    seen = set()
    for y in range(max(0, y0), min(h, y1), step):
        r = rows[y]
        for x in range(max(0, x0), min(w, x1), step):
            seen.add(tuple(r[x * 3:x * 3 + 3]))
    return seen


def has_bars(cs):
    """The fake redux's yellow, green and blue bars are among the colours."""
    yellow = any(r > 150 and g > 150 and b < 60 for r, g, b in cs)
    green = any(r < 60 and g > 150 and b < 60 for r, g, b in cs)
    blue = any(r < 60 and g < 60 and b > 150 for r, g, b in cs)
    return yellow and green and blue


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
        fh.write('#!/bin/sh\nSPLASHEDIT_FAKE_REDUX=320x240 SPLASHEDIT_FAKE_REDUX_FLIP=6 exec "%s" "$@"\n' % display)
    os.chmod(redux, os.stat(redux).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    env = dict(os.environ, SPLASHEDIT_REDUX=redux, SPLASHEDIT_PSXSPLASH=engine)
    base = (shots + '-' + name) if shots else os.path.join(work, name)
    args = [a.replace('{mid}', base + '-mid.png') for a in args]
    cmd = [editor, '--size', '1600x960', '--project', project, '--scene', os.path.join(project, 'courtyard.scene'),
           '--play-settings', settings, '--wait', '5', '--key', 'f5', '--wait', '240'] + args + ['--wait', '20', '--screenshot', base + '.png']
    r = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=300)
    lines = (r.stdout + r.stderr).strip().splitlines()
    controls = {}
    for l in lines:
        if l.startswith('control ') and ' at ' in l:
            cname, at = l[len('control '):].rsplit(' at ', 1)
            a, b = at.split(' ')
            controls[cname] = tuple(int(v) for v in a.split(',') + b.split(','))
    missing = [l for l in lines if 'no such control drawn' in l or 'no such window open' in l]
    check('%s: every control and window the script names was there' % name, not missing, missing)
    cfg = open(settings).read() if os.path.exists(settings) else ''
    return lines, controls, cfg, base


def last(lines, prefix):
    got = [l for l in lines if l.startswith(prefix)]
    return got[-1] if got else ''


def main_controls(c):
    """The main window's controls, without the panels' windows' and the ones that change by the frame."""
    return {k: v for k, v in c.items() if ':' not in k and not k.startswith('game.fps')}


# 0. Never detached: the layout to come back to.
lines, base_c, cfg, _ = run('base', [])
check('base: the Game view is beside the Scene tab', 'game.frame' in base_c and 'viewport.split' in base_c, sorted(k for k in base_c if k.startswith('game.')))

# 1. Out: a second window with the game in it, the Scene tab takes the room; the keyboard follows the focus; closed, back.
lines, c, cfg, shot = run('out', DETACH_GAME + ['--in', 'game', '--keydown', 'z', '--wait', '10', '--in', 'game', '--keyup', 'z', '--wait', '5',
                                               '--shot', '{mid}', '--focus-window', 'main', '--wait', '10', '--in', 'game', '--keydown', 'z',
                                               '--wait', '10', '--close-window', 'game', '--wait', '30'])
check('out: it went out', 'detach: game out' in lines, [l for l in lines if l.startswith('detach')])
check('out: its window opened', any(l.startswith('panel window game: opened ') for l in lines), [l for l in lines if l.startswith('panel window')])
check('out: the main window no longer shows the game', any(l == 'game: shown no every tab on own window' for l in lines),
      [l for l in lines if l.startswith('game: shown')])
gpng = shot + '-mid-game.png'
check('out: the game window was written', os.path.exists(gpng), gpng)
if os.path.exists(gpng):
    cs = colours(gpng)
    check('out: the game window is not blank (the bars show)', len(cs) >= 8 and has_bars(cs), (len(cs), sorted(cs)[:12]))
mpng = shot + '-mid.png'
if os.path.exists(mpng) and 'game.frame' in base_c:
    cs = colours(mpng, base_c['game.frame'])
    check('out: where the split showed the game the main window shows the scene', not has_bars(cs), sorted(cs)[:12])
focused = [l for l in lines if l.startswith('window: game drawn')]
check('out: its window has the keyboard as it opens', bool(focused) and focused[0].endswith(' focused'), focused)
pads = [l for l in lines if l.startswith('game: pad ')]
check('out: Z in its window is the pad\'s cross', 'game: pad bfff' in pads, pads)
check('out: the main window focused, its window lets the keyboard go', any(l.startswith('window: game drawn') and not l.endswith('focused') for l in lines), focused)
check('out: Z there then is no button', pads.count('game: pad bfff') == 1, pads)
check('out: closing its window put it back', 'detach: game back' in lines and 'panel window game: closed' in lines,
      [l for l in lines if l.startswith('detach') or l.startswith('panel window')])
check('out: back beside the Scene tab', last(lines, 'game: shown') == 'game: shown yes every tab on', last(lines, 'game: shown'))
mc, mb = main_controls(c), main_controls(base_c)
moved = sorted(k for k in set(mc) | set(mb) if mc.get(k) != mb.get(k))
check('out: back, the main window\'s controls are where they were', not moved, [(k, mb.get(k), mc.get(k)) for k in moved][:8])
check('out: saved back, with where its window was', 'game_detached=0\n' in cfg and 'game_window=' in cfg, cfg)

# 2. Its own Attach button.
lines, c, cfg, shot = run('attach', DETACH_GAME + ['--in', 'game', '--click', '@game.attach', '--wait', '30'])
check('attach: Attach put it back', 'detach: game back' in lines and 'panel window game: attached' in lines,
      [l for l in lines if l.startswith('detach') or l.startswith('panel window')])
check('attach: the split is back', 'game.frame' in c and 'viewport.split' in c, sorted(k for k in c if k.startswith('game.')))

# 3. Saved out: the window opens at the next Play where it was, at its size.
lines, c, cfg, shot = run('saved', [], 'game_detached=1\ngame_window=100,120,500,400\n')
check('saved: its window opened at the saved size', 'panel window game: open 500x400 focus yes' in lines,
      [l for l in lines if l.startswith('panel window')])
check('saved: no split in the main window', 'viewport.split' not in c and 'game.frame' not in c, sorted(k for k in c if k.startswith('game')))
check('saved: its controls are listed', 'game:game.frame' in c and 'game:game.attach' in c, sorted(k for k in c if k.startswith('game:')))
gpng = shot + '-game.png'
if os.path.exists(gpng) and 'game:game.frame' in c:
    cs = colours(gpng, c['game:game.frame'])
    check('saved: the game window is not blank (the bars show)', has_bars(cs), sorted(cs)[:12])
else:
    check('saved: the game window was written', False, gpng)
check('saved: still out, still there', 'game_detached=1\n' in cfg and 'game_window=100,120,500,400\n' in cfg, cfg)

# 4. The Console's Own window, from the status bar's popup; closed, back in the popup's place.
lines, c, cfg, shot = run('console', ['--click', '@playstatus', '--wait', '10', '--click', '@play.detach', '--wait', '30', '--shot', '{mid}',
                                      '--close-window', 'console', '--wait', '30'])
check('console: it went out', 'detach: console out' in lines and any(l.startswith('panel window console: opened') for l in lines),
      [l for l in lines if l.startswith('detach') or l.startswith('panel window')])
check('console: its window shows the output', 'window: console drawn 760x420 output' in lines, [l for l in lines if l.startswith('window:')])
cpng = shot + '-mid-console.png'
if os.path.exists(cpng):
    check('console: the console window is not blank', len(colours(cpng)) >= 8, len(colours(cpng)))
else:
    check('console: the console window was written', False, cpng)
check('console: closing it put it back', 'detach: console back' in lines and 'panel window console: attached' in lines,
      [l for l in lines if l.startswith('detach') or l.startswith('panel window')])
check('console: saved back', 'console_detached=0\n' in cfg and 'console_window=' in cfg, cfg)

print('detachcheck: %d failure(s)' % fails if fails else 'detachcheck: all passed')
sys.exit(1 if fails else 0)
