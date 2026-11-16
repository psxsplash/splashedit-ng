# The Game view on every tab and the docked console (#345), driven through
# the GUI. Play runs the fake pcsx-redux gameviewcheck.py uses (emulator_tests
# with SPLASHEDIT_FAKE_REDUX=320x240), which prints "fake redux up".
#  - On the UI tab and on a script tab the Game view stays beside the tab,
#    right of or below it, not over it.
#  - View > Game View on Every Tab off (or game_every_tab=0 in the play
#    settings): the Game view shows on the Scene tab only, as before.
#  - View > Console docks Play's output as a Console tab under the middle
#    panel, beside the Game view, no popup open; Pop up puts it back in the
#    status bar's popup. Docked, the status bar entry brings the tab back.
#  - Both choices are saved in the play settings (--play-settings).
# usage: python3 gamedockcheck.py <splashedit> [screenshot prefix]   (needs a display: xvfb-run -a)
import os, shutil, stat, subprocess, sys, tempfile

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
EDIT_SCRIPT = '1410,419'  # Game Logic's "Edit script" button at 1600x960 (as in scripttabscheck.py)
OPEN_SCRIPT = ['--select', 'Game Logic', '--click', EDIT_SCRIPT, '--wait', '10']


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def run(name, before, args, cfg_text=None):
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
    out = os.path.join(shots + '-' + name + '.png') if shots else os.path.join(project, 'shot.png')
    cmd = [editor, '--size', '1600x960', '--project', project, '--scene', os.path.join(project, 'courtyard.scene'),
           '--play-settings', settings, '--wait', '5'] + before + ['--key', 'f5', '--wait', '240'] + args + \
          ['--wait', '20', '--screenshot', out]
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
    return lines, controls, cfg


def beside(a, b):
    """Rect a wholly right of or below rect b, no overlap."""
    return a[0] >= b[2] or a[1] >= b[3]


def last(lines, prefix):
    got = [l for l in lines if l.startswith(prefix)]
    return got[-1] if got else ''


# 1. The UI tab: the Game view stays, right of the canvas.
lines, c, cfg = run('ui', [], ['--click', '@tab.ui', '--wait', '30'])
check('UI tab: the UI canvas is drawn', 'ui.canvas' in c, sorted(c))
check('UI tab: the Game view is drawn', 'game.frame' in c and 'game.pause' in c, sorted(k for k in c if k.startswith('game.')))
check('UI tab: the scripted line says shown', last(lines, 'game: shown') == 'game: shown yes every tab on', last(lines, 'game: shown'))
if 'game.frame' in c and 'ui.canvas' in c:
    check('UI tab: the Game view is beside the canvas, not over it', beside(c['game.frame'], c['ui.canvas']), (c['game.frame'], c['ui.canvas']))
check('UI tab: the split bar is drawn', 'viewport.split' in c, sorted(c))

# 2. A script tab: the Game view stays beside the code.
lines, c, cfg = run('script', OPEN_SCRIPT, ['--wait', '10'])
check('script tab: a script tab is open', 'tab.0' in c, sorted(c))
check('script tab: the Game view is drawn', 'game.frame' in c, sorted(k for k in c if k.startswith('game.')))
check('script tab: the scripted line says shown', last(lines, 'game: shown') == 'game: shown yes every tab on', last(lines, 'game: shown'))
if 'game.frame' in c and 'tab.0' in c:
    check('script tab: the Game view is right of the tab strip start', c['game.frame'][0] > c['tab.0'][0], (c['game.frame'], c['tab.0']))

# 3. Off from the play settings: the UI tab alone, as before #345; the Scene tab still splits.
lines, c, cfg = run('off', [], ['--click', '@tab.ui', '--wait', '30'], 'game_every_tab=0\n')
check('off: the UI tab has no Game view', 'game.frame' not in c and 'viewport.split' not in c, sorted(k for k in c if k.startswith('game.')))
check('off: the scripted line says not shown', last(lines, 'game: shown') == 'game: shown no every tab off', last(lines, 'game: shown'))
check('off: the Scene tab had the Game view before the click',
      any(l == 'game: shown yes every tab off' for l in lines), [l for l in lines if l.startswith('game: shown')])

# 4. Turned off from the View menu and saved.
lines, c, cfg = run('menu', [], ['--click', '@menu.view', '--wait', '3', '--click', '@menu.view/Game View on Every Tab', '--wait', '5',
                                 '--click', '@tab.ui', '--wait', '30'])
check('menu: Game View on Every Tab off is saved', 'game_every_tab=0\n' in cfg, cfg)
check('menu: the UI tab has no Game view then', 'game.frame' not in c, sorted(k for k in c if k.startswith('game.')))

# 5. View > Console docks the output beside the Game view, no popup; saved.
lines, c, cfg = run('dock', [], ['--click', '@tab.ui', '--wait', '10', '--click', '@menu.view', '--wait', '3',
                                 '--click', '@menu.view/Console', '--wait', '30'])
check('dock: the Console tab and panel are drawn', 'tab_console' in c and 'console.panel' in c, sorted(c))
check('dock: it says docked', last(lines, 'console: docked') == 'console: docked', last(lines, 'console: docked'))
check('dock: it shows what redux printed', any(l == 'console: play shown 1 of 1' for l in lines),
      [l for l in lines if l.startswith('console:')])
check('dock: no popup is open', 'popups open: no' in lines, [l for l in lines if l.startswith('popups open')])
check('dock: the Game view is still drawn', 'game.frame' in c, sorted(k for k in c if k.startswith('game.')))
if 'game.frame' in c and 'console.panel' in c:
    check('dock: the Game view is above the console', c['game.frame'][3] <= c['console.panel'][1], (c['game.frame'], c['console.panel']))
check('dock: saved', 'console_docked=1\n' in cfg, cfg)

# 6. Docked from the settings: the Project tab in front, then the status bar entry brings the Console back.
lines, c, cfg = run('status', [], ['--click', '@tab_project', '--wait', '10', '--click', '@playstatus', '--wait', '20'], 'console_docked=1\n')
check('status: the Console tab came back to the front', 'console.panel' in c, sorted(c))
check('status: no popup is open', 'popups open: no' in lines, [l for l in lines if l.startswith('popups open')])

# 7. Pop up: back to the popup off the status bar, opened, and saved.
lines, c, cfg = run('popup', [], ['--click', '@play.popup', '--wait', '20'], 'console_docked=1\n')
check('popup: the Console tab is gone', 'tab_console' not in c and 'console.panel' not in c, sorted(c))
check('popup: the output popup is open', 'popups open: no' not in lines and 'play.dock' in c,
      [l for l in lines if l.startswith('popups open')])
check('popup: saved', 'console_docked=0\n' in cfg, cfg)

print('gamedockcheck: %d failure(s)' % fails if fails else 'gamedockcheck: all passed')
sys.exit(1 if fails else 0)
