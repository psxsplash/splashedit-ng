# The Game view, Play's Console, the Scene, the Inspector, the Project panel
# and the Timeline out in OS windows of their own (#345), driven through the GUI. Play runs the fake pcsx-redux gamedockcheck.py uses
# (emulator_tests with SPLASHEDIT_FAKE_REDUX=320x240: colour bars).
#  - View > Panels > Game View in Own Window while a game runs opens a second window
#    with the game's picture in it (not blank: the bars), and the main
#    window's tab takes the room the split had.
#  - The keyboard is the controller in that window while it has the focus,
#    and not once the main window has it.
#  - Closing the window (its close button) or its Attach puts the Game view
#    back: the main window's controls are where they were before it left.
#  - Out or not, and where the window was, are saved in the play settings
#    (--play-settings); saved out, the window opens there at the next Play.
#  - The Console's Own window does the same for Play's output.
#  - Saved out, the Game view's window opens as the editor starts and stays
#    open across Plays: a Play neither opens it again nor takes the keyboard
#    from the main window.
#  - View > Panels > Scene / Inspector / Project / Timeline in Own Window: the panel
#    draws in its own window (not blank), the main window gives its room to
#    the others (no hole), its Attach puts it back where it was.
#  - --in <panel> with --ctrl-held: a Ctrl-click in the Scene's window adds to
#    the selection there (the click lands in that window, with Ctrl).
#  - View > Panels > Scene View in Own Window: the 3D view draws the scene in
#    its own window (not blank), a click on a marker there selects, its move
#    gizmo drags, its shortcuts save, a model dropped on it lands where the
#    pointer meets the scene there; the main window gives the Scene tab's
#    room to the bottom dock (no hole), to the Game view while a game runs,
#    or with neither says where it went. Closed or Attach, it comes back where
#    it was; out or not and where are saved (view_detached=, view_window=).
# Detached windows are written beside each screenshot as <shot>-<panel>.png.
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
DETACH_GAME = ['--click', '@menu.view', '--wait', '3', '--click', '@menu.view/Panels', '--wait', '3',
               '--click', '@menu.view/Panels/Game View in Own Window', '--wait', '30']


def detach(title):
    return ['--click', '@menu.view', '--wait', '3', '--click', '@menu.view/Panels', '--wait', '3',
            '--click', '@menu.view/Panels/%s in Own Window' % title, '--wait', '30']


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


def run(name, args, cfg_text=None, play=True):
    global last_project
    project = os.path.join(tempfile.mkdtemp(), 'p')
    last_project = project
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
           '--play-settings', settings, '--wait', '5'] + (['--key', 'f5', '--wait', '240'] if play else []) + args + \
          ['--wait', '20', '--screenshot', base + '.png']
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
split_c = base_c
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

# 5. Saved out, the Game view's window opens with the editor and stays open
#    across Plays: Play, stop, Play again from the main window (the keyboard
#    there) neither opens it again nor gives the game the keyboard.
lines, c, cfg, shot = run('replay', ['--focus-window', 'main', '--wait', '10', '--key', 'f5', '--wait', '240', '--key', 'f5', '--wait', '60',
                                     '--key', 'f5', '--wait', '240'], 'game_detached=1\ngame_window=100,120,500,400\n', play=False)
opened = [l for l in lines if l.startswith('panel window game: opened')]
check('replay: its window opened once, as the editor started', len(opened) == 1 and not opened[0].endswith(' raised'), opened)
check('replay: no Play closed it', 'panel window game: closed' not in lines, [l for l in lines if l.startswith('panel window game')])
drawn = [l for l in lines if l.startswith('window: game drawn')]
shown = ''.join('f' if ' frames' in l else 'w' for l in drawn)
check('replay: it showed the first game, waited, then the second', 'fwf' in ''.join(ch for i, ch in enumerate(shown) if i == 0 or shown[i - 1] != ch),
      drawn)
check('replay: no Play gave the game the keyboard', bool(drawn) and not any(' frames' in l and l.endswith(' focused') for l in drawn), drawn)
check('replay: still out', 'game_detached=1\n' in cfg, cfg)

# 6. The Scene, the Inspector, the Project panel and the Timeline: out from the
#    View menu (the main window gives its room to the rest), and saved out
#    then put back with their window's Attach.
NEWCS = ['--click', '@newcs', '--wait', '20']  # the courtyard has no cutscene: one, open in the Timeline
lines, plain_c, cfg, _ = run('plain', [], play=False)
lines, tl_c, cfg, _ = run('plain-tl', NEWCS, play=False)
check('plain: the panels are in the main window', all(k in plain_c for k in ('treelist.panel', 'inspector.tab/Inspector', 'tab_project')) and
      'timeline.body' not in plain_c, sorted(k for k in plain_c if '.' not in k)[:12])
PANELS = [  # name, title, a control of it in the main window, one in its own, how the Scene view grows, before (args)
    ('scene', 'Scene', 'treelist.panel', 'tree.Crate', 'left', []),
    ('inspector', 'Inspector', 'inspector.tab/Inspector', 'inspector.tab/Inspector', 'right', []),
    ('project', 'Project', 'tab_project', 'assets.tree/.', 'down', []),
    ('timeline', 'Timeline', 'tab_timeline', 'addtrack', 'down', NEWCS),
]
for name, title, mark, inner, grows, before in PANELS:
    base_c = tl_c if before else plain_c
    lines, c, cfg, shot = run(name + '-out', before + detach(title) + ['--shot', '{mid}'], play=False)
    check('%s-out: it went out' % name, 'detach: %s out' % name in lines and
          any(l.startswith('panel window %s: opened ' % name) and l.endswith(' raised') for l in lines),
          [l for l in lines if l.startswith('detach') or l.startswith('panel window')])
    check('%s-out: its window draws it' % name, any(l.startswith('window: %s drawn ' % name) for l in lines) and
          all('%s:%s' % (name, k) in c for k in (name + '.body', name + '.attach', inner)), sorted(k for k in c if k.startswith(name + ':'))[:8])
    wpng = shot + '-mid-%s.png' % name
    check('%s-out: its window was written, not blank' % name, os.path.exists(wpng) and len(colours(wpng)) >= 8, wpng)
    check('%s-out: gone from the main window' % name, mark not in c, sorted(k for k in c if ':' not in k and k.startswith(mark.split('.')[0]))[:6])
    b, a = base_c.get('vp.view'), c.get('vp.view')
    grew = bool(a and b) and (a[0] < b[0] if grows == 'left' else a[2] > b[2] if grows == 'right' else a[3] > b[3])
    check('%s-out: the Scene view takes its room (%s)' % (name, grows), grew, (b, a))
    check('%s-out: saved out' % name, '%s_detached=1\n' % name in cfg and '%s_window=' % name in cfg, cfg)
    lines, c, cfg, shot = run(name + '-back', before + ['--wait', '10', '--in', name, '--click', '@%s.attach' % name, '--wait', '30'],
                              '%s_detached=1\n%s_window=40,50,620,500\n' % (name, name), play=False)
    check('%s-back: saved out, it opened at the saved size' % name, any(l.startswith('panel window %s: opened 620x500' % name) for l in lines),
          [l for l in lines if l.startswith('panel window ' + name)])
    check('%s-back: Attach put it back' % name, 'detach: %s back' % name in lines and 'panel window %s: attached' % name in lines,
          [l for l in lines if l.startswith('detach') or l.startswith('panel window ' + name)])
    mc, mb = main_controls(c), main_controls(base_c)
    moved = sorted(k for k in set(mc) | set(mb) if mc.get(k) != mb.get(k))
    check('%s-back: back, the main window\'s controls are where they were' % name, not moved, [(k, mb.get(k), mc.get(k)) for k in moved][:8])
    check('%s-back: saved back, with where its window was' % name, '%s_detached=0\n' % name in cfg and '%s_window=40,50,620,500\n' % name in cfg, cfg)

# 7. --in with --ctrl-held: the Ctrl-click lands in the Scene's own window,
#    with Ctrl (a second object joins the selection). --ctrl-held between
#    --in and its click must leave --in on the click.
lines, c, cfg, shot = run('ctrl', detach('Scene') + ['--in', 'scene', '--click', '@tree.Crate', '--wait', '10',
                                                     '--in', 'scene', '--ctrl-held', '--click', '@tree.Fill', '--wait', '10'], play=False)
sel = [l for l in lines if l.startswith('selection: ')]
check('ctrl: Ctrl-click in the Scene\'s window added to the selection', bool(sel) and sel[-1] == 'selection: 2 primary=Fill all=Crate;Fill', sel)

# 8. The Scene view (the 3D viewport) out in its own window.
def object_pos(project, name):
    import json
    def find(objs):
        for o in objs:
            if o.get('name') == name: return o['transform']['position']
            got = find(o.get('children', []))
            if got: return got
    return find(json.load(open(os.path.join(project, 'courtyard.scene')))['objects'])


torch0 = object_pos(example, 'Torch Light')
lines, c, cfg, shot = run('view-out', detach('Scene View') + ['--in', 'view', '--click', '@vp.marker.Torch Light', '--wait', '10',
                                                              '--in', 'view', '--drag', '@vp.move.y,0,0,0,-50', '--wait', '10',
                                                              '--in', 'view', '--key', 'ctrl+s', '--wait', '10', '--shot', '{mid}'], play=False)
check('view-out: it went out', 'detach: view out' in lines and
      any(l.startswith('panel window view: opened ') and l.endswith(' raised') for l in lines),
      [l for l in lines if l.startswith('detach') or l.startswith('panel window')])
check('view-out: its window draws the Scene view', any(l.startswith('window: view drawn ') and l.endswith(' scene') for l in lines) and
      all('view:' + k in c for k in ('vp.view', 'view.attach', 'vp.camera')), sorted(k for k in c if k.startswith('view:'))[:8])
vpng = shot + '-mid-view.png'
if os.path.exists(vpng) and 'view:vp.view' in c:
    x0, y0, x1, y1 = c['view:vp.view']
    cs = colours(vpng, (x0 + 20, y0 + 160, x1 - 20, y1 - 50), 2)
    check('view-out: its window renders the scene (not blank)', len(cs) >= 40, len(cs))
else:
    check('view-out: its window was written', False, vpng)
sel = [l for l in lines if l.startswith('selection: ')]
check('view-out: a click on a marker in its window selects', 'selection: 1 primary=Torch Light all=Torch Light' in sel, sel)
torch = object_pos(last_project, 'Torch Light')
check('view-out: its move gizmo dragged the light up, Ctrl+S there saved', bool(torch) and torch[1] > torch0[1] + 0.05 and
      abs(torch[0] - torch0[0]) < 1e-3 and abs(torch[2] - torch0[2]) < 1e-3, (torch0, torch))
check('view-out: gone from the main window', 'vp.view' not in c and 'view.away' not in c, sorted(k for k in c if ':' not in k and k.startswith('vp'))[:6])
b, a = plain_c.get('vp.view'), c.get('tab_project')
check('view-out: the Project panel takes the Scene tab\'s room (no hole)', bool(a and b) and abs(a[1] - b[1]) <= 2, (b, a))
check('view-out: saved out', 'view_detached=1\n' in cfg and 'view_window=' in cfg, cfg)

lines, c, cfg, shot = run('view-close', detach('Scene View') + ['--close-window', 'view', '--wait', '30'], play=False)
check('view-close: closing its window put it back', 'detach: view back' in lines and 'panel window view: closed' in lines,
      [l for l in lines if l.startswith('detach') or l.startswith('panel window view')])
mc, mb = main_controls(c), main_controls(plain_c)
moved = sorted(k for k in set(mc) | set(mb) if mc.get(k) != mb.get(k))
check('view-close: back, the main window\'s controls are where they were', not moved, [(k, mb.get(k), mc.get(k)) for k in moved][:8])
check('view-close: saved back, with where its window was', 'view_detached=0\n' in cfg and 'view_window=' in cfg, cfg)

lines, c, cfg, shot = run('view-back', ['--wait', '10', '--shot', '{mid}', '--in', 'view', '--click', '@view.attach', '--wait', '30'],
                          'view_detached=1\nview_window=40,50,620,500\n', play=False)
check('view-back: saved out, it opened at the saved size', any(l.startswith('panel window view: opened 620x500') for l in lines),
      [l for l in lines if l.startswith('panel window view')])
vpng = shot + '-mid-view.png'
check('view-back: its window was written, not blank', os.path.exists(vpng) and len(colours(vpng)) >= 40, vpng)
check('view-back: Attach put it back', 'detach: view back' in lines and 'panel window view: attached' in lines,
      [l for l in lines if l.startswith('detach') or l.startswith('panel window view')])
mc = main_controls(c)
moved = sorted(k for k in set(mc) | set(mb) if mc.get(k) != mb.get(k))
check('view-back: back, the main window\'s controls are where they were', not moved, [(k, mb.get(k), mc.get(k)) for k in moved][:8])
check('view-back: saved back, with where its window was', 'view_detached=0\n' in cfg and 'view_window=40,50,620,500\n' in cfg, cfg)

# A model released on its window lands where the pointer meets the scene
# there (on the floor below the middle), as a drop on the main window's would.
box = os.path.join(work, 'dropbox.obj')
with open(box, 'w') as fh:
    fh.write('v -0.25 0 -0.25\nv 0.25 0 -0.25\nv 0.25 0.5 -0.25\nv -0.25 0.5 -0.25\n'
             'v -0.25 0 0.25\nv 0.25 0 0.25\nv 0.25 0.5 0.25\nv -0.25 0.5 0.25\n'
             'f 1 2 3 4\nf 5 8 7 6\nf 1 5 6 2\nf 4 3 7 8\nf 1 4 8 5\nf 2 6 7 3\n')
lines, c, cfg, shot = run('view-drop', ['--wait', '10', '--in', 'view', '--drop-now', box + '@@vp.view,0,180', '--wait', '5', '--wait-import',
                                        '--wait', '10', '--in', 'view', '--key', 'ctrl+s', '--wait', '10'],
                          'view_detached=1\nview_window=40,50,960,640\n', play=False)
dropped = object_pos(last_project, 'dropbox')
check('view-drop: the model dropped on its window was added', dropped is not None,
      [l for l in lines if 'import' in l.lower()][-4:])
check('view-drop: where the pointer met the floor there', dropped is not None and abs(dropped[1]) < 0.3 and
      (abs(dropped[0]) > 1e-3 or abs(dropped[2]) > 1e-3), dropped)

# Out while a game runs: the game takes the whole Scene tab, no split.
lines, c, cfg, shot = run('view-game', [], 'view_detached=1\nview_window=40,50,620,500\n')
g, g0 = c.get('game.frame'), split_c.get('game.frame')
check('view-game: the Game view takes the Scene tab\'s room, no split', 'viewport.split' not in c and bool(g and g0) and
      (g[2] - g[0]) * (g[3] - g[1]) > (g0[2] - g0[0]) * (g0[3] - g0[1]), (g0, g))
check('view-game: its window still draws the scene', any(l.startswith('window: view drawn ') for l in lines) and 'view:vp.view' in c,
      [l for l in lines if l.startswith('window: view')][:2])

# Out with the Project panel out too and no game: nothing else for the room, a
# note where the Scene view went, whose Attach brings it back.
lines, c, cfg, shot = run('view-away', ['--wait', '10', '--shot', '{mid}', '--click', '@view.away.attach', '--wait', '30'],
                          'view_detached=1\nproject_detached=1\n', play=False)
check('view-away: the note fills the Scene tab\'s room', 'detach: view back' in lines,
      [l for l in lines if l.startswith('detach') or l.startswith('@')])
check('view-away: back in the main window', 'vp.view' in c and 'view.away' not in c, sorted(k for k in c if k.startswith('v'))[:6])

print('detachcheck: %d failure(s)' % fails if fails else 'detachcheck: all passed')
sys.exit(1 if fails else 0)
