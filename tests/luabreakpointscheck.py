# Lua breakpoints (src/editor/luadebug.hh, debugger_view.hh), driven through
# the GUI. A click in a Lua tab's gutter sets a breakpoint on that line
# ("luaeditor: breakpoint <chunk>:<line> on, N set"), saved in the project's
# .breakpoints.json; a second click clears it, and the file goes with the
# last one. A click in the code beside it sets nothing.
#
# Play then runs a stand-in pcsx-redux (SPLASHEDIT_REDUX, a shell script):
# it copies the luabp.txt Play wrote, prints psxsplash's stop report for
# line 9, waits for the editor's luabp.cmd, prints a resume and a second stop,
# waits again, then copies luabp.txt once more after a breakpoint is added
# while playing. It execs emulator_tests (beside the editor) with
# SPLASHEDIT_FAKE_REDUX set, which publishes the display block so the Game
# view opens. The editor must show the stop (the script tab opens, "play:
# stopped 1 at scripts/game_logic.lua:9", the Debugger tab's locals and
# globals), send "1 step" for Step and "2 continue" for Continue, and the
# Debugger tab must not exist before Play or with an engine that never says
# its debugger is on.
# usage: python3 luabreakpointscheck.py <splashedit> [screenshot]   (needs a display: xvfb-run -a)
import json, os, re, shutil, stat, subprocess, sys, tempfile

editor = os.path.abspath(sys.argv[1])
display = os.path.join(os.path.dirname(editor), 'emulator_tests')
shot = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else None
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
fails = 0
EDIT_SCRIPT = '1410,419'  # Game Logic's "Edit script" button at 1600x960 (as in scripteditorcheck.py)
OPEN = ['--select', 'Game Logic', '--click', EDIT_SCRIPT, '--wait', '10']
CHUNK = 'scripts/game_logic.lua'
SCRIPT = '''-- Courtyard demo: scene-wide game logic.
local frames = 0

function onCreate(self)
end

function onUpdate(self, dt)
    frames = frames + 1
    local doubled = frames * 2
    score = doubled + 1
    label = "frame " .. frames
end
'''


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def project_copy(breakpoints=None):
    project = os.path.join(tempfile.mkdtemp(), 'courtyard')
    shutil.copytree(example, project)
    with open(os.path.join(project, 'scripts', 'game_logic.lua'), 'w', newline='\n') as fh:
        fh.write(SCRIPT)
    if breakpoints is not None:
        with open(os.path.join(project, '.breakpoints.json'), 'w') as fh:
            json.dump({'breakpoints': breakpoints}, fh)
    return project


def run(project, args, env=None, timeout=240):
    cmd = [editor, '--size', '1600x960', '--project', project, '--scene', os.path.join(project, 'courtyard.scene'),
           '--wait', '5'] + args + ['--wait', '5', '--screenshot', os.path.join(project, 'shot.png')]
    r = subprocess.run(cmd, capture_output=True, text=True, env=env or ENV, timeout=timeout)
    return (r.stdout + r.stderr).strip().splitlines()


def saved(project):
    path = os.path.join(project, '.breakpoints.json')
    if not os.path.exists(path):
        return None
    with open(path) as fh:
        return json.load(fh).get('breakpoints')


def gutter(lines):
    """Where line 1 of the open script's gutter is: (x, top, step)."""
    for l in lines:
        m = re.match(r'luaeditor: gutter (\S+) x (\d+)-(\d+) line1 (-?\d+) step (\d+)', l)
        if m and m.group(1) == CHUNK:
            x0, x1, y, step = int(m.group(2)), int(m.group(3)), int(m.group(4)), int(m.group(5))
            return (x0 + x1) // 2, y, step
    return None


def at(g, line, x=None):
    cx, y, step = g
    return '%d,%d' % (x if x is not None else cx, y + (line - 1) * step + step // 2)


work = tempfile.mkdtemp()
engine = os.path.join(work, 'psxsplash.ps-exe')
with open(engine, 'wb') as fh:
    fh.write(b'PS-X EXE fake')
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')

# Where the gutter is, from a run that only opens the script.
project = project_copy()
lines = run(project, OPEN)
g = gutter(lines)
check('the open script reports its gutter', g is not None, [l for l in lines if 'gutter' in l][:3])
if g is None:
    print('%d failures' % fails)
    sys.exit(1)
check('nothing saved before a click', saved(project) is None, saved(project))

# A click in the gutter sets a breakpoint on that line; a click in the code beside it does not.
project = project_copy()
lines = run(project, OPEN + ['--click', at(g, 10, g[0] + 300), '--wait', '5', '--click', at(g, 9), '--wait', '5'])
bp = [l for l in lines if l.startswith('luaeditor: breakpoint ')]
check('a gutter click sets one breakpoint', bp == ['luaeditor: breakpoint %s:9 on, 1 set' % CHUNK], bp)
check('it is saved with the project', saved(project) == {CHUNK: [9]}, saved(project))

# A second click on it clears it, and the file goes with the last one.
lines = run(project, OPEN + ['--click', at(g, 9), '--wait', '5'])
bp = [l for l in lines if l.startswith('luaeditor: breakpoint ')]
check('the saved breakpoint loads, and a click clears it', bp == ['luaeditor: breakpoint %s:9 off, 0 set' % CHUNK], bp)
check('no breakpoints, no file', saved(project) is None, saved(project))

# Play with the stand-in engine.
log = os.path.join(work, 'fake.log')
redux = os.path.join(work, 'fake-redux.sh')
with open(redux, 'w', newline='\n') as fh:
    fh.write('''#!/bin/sh
dir=""; prev=""
for a in "$@"; do [ "$prev" = "-pcdrvbase" ] && dir="$a"; prev="$a"; done
waitcmd() {
  i=0
  while [ $i -lt 600 ]; do
    if [ -e "$dir/luabp.cmd" ] && grep -q "^$1\\$" "$dir/luabp.cmd"; then cp "$dir/luabp.cmd" "%(log)s.$2"; return 0; fi
    sleep 0.1; i=$((i + 1))
  done
  return 1
}
(
  cp "$dir/luabp.txt" "%(log)s.bp1" 2>/dev/null
  echo "psxsplash: lua debugger on, 1 breakpoints"
  sleep 1
  echo "psxsplash: lua break 1 9 %(chunk)s"
  echo "psxsplash: lua local self table {__actor_id=3, __cpp_ptr=userdata}"
  echo "psxsplash: lua local dt number 4096"
  echo "psxsplash: lua local doubled number 60"
  echo "psxsplash: lua upvalue frames number 30"
  echo 'psxsplash: lua global label string "frame 29"'
  echo "psxsplash: lua global score number 59"
  echo "psxsplash: lua break end 1"
  waitcmd "1 step" cmd1 || exit 0
  echo "psxsplash: lua resume 1 step"
  echo "psxsplash: lua break 2 10 %(chunk)s"
  echo "psxsplash: lua local doubled number 60"
  echo "psxsplash: lua global score number 61"
  echo "psxsplash: lua break end 2"
  waitcmd "2 continue" cmd2 || exit 0
  echo "psxsplash: lua resume 2 continue"
  i=0
  while [ $i -lt 600 ] && [ "$(head -n1 "$dir/luabp.txt")" = "$(head -n1 "%(log)s.bp1")" ]; do sleep 0.1; i=$((i + 1)); done
  cp "$dir/luabp.txt" "%(log)s.bp2"
) &
SPLASHEDIT_FAKE_REDUX=1 exec "%(display)s"
''' % {'log': log, 'chunk': CHUNK, 'display': display})
os.chmod(redux, os.stat(redux).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
PLAY = dict(os.environ, SPLASHEDIT_REDUX=redux, SPLASHEDIT_PSXSPLASH=engine)


def read(path):
    try:
        with open(path) as fh:
            return fh.read()
    except OSError:
        return None


# Before Play there is no Debugger tab to click.
project = project_copy({CHUNK: [9]})
lines = run(project, ['--click', '@tab_debugger', '--wait', '5'])
check('no Debugger tab before Play', any('@tab_debugger: no such control drawn' in l for l in lines),
      [l for l in lines if 'tab_debugger' in l])

# The stop opens the script by itself (no tab is open before), Step and Continue resume it.
project = project_copy({CHUNK: [9]})
shots = ['--shot', shot] if shot else []
lines = run(project, ['--key', 'f5', '--wait', '30', '--wait-stop', '--wait', '30'] + shots +
            ['--click', '@debug.step', '--wait', '30', '--wait-stop', '--wait', '20', '--click', '@debug.continue', '--wait', '30'] +
            ['--click', at(g, 11), '--wait', '60'], env=PLAY)
missing = [l for l in lines if 'no such control drawn' in l or l.startswith('--wait-stop')]
check('every control the script names was drawn, every stop came', not missing, missing)
check('Play wrote the breakpoints for the engine', read(log + '.bp1') == '1\n9 %s\n' % CHUNK, read(log + '.bp1'))
stops = [l for l in lines if l.startswith('play: stopped ')]
check('the editor shows both stops', stops == ['play: stopped 1 at %s:9' % CHUNK, 'play: stopped 2 at %s:10' % CHUNK], stops)
dbg = [l for l in lines if l.startswith('debugger: ')]
check('the Debugger tab lists the first stop', 'debugger: stopped 1 at %s:9 locals 4 globals 2' % CHUNK in dbg, dbg)
check('then the second', 'debugger: stopped 2 at %s:10 locals 1 globals 1' % CHUNK in dbg, dbg)
check('and runs again after Continue', bool(dbg) and dbg[-1] == 'debugger: running', dbg[-3:])
check('the stop opened the script in a tab', gutter(lines) is not None, [l for l in lines if 'gutter' in l][:2])
check('Step sent "1 step"', read(log + '.cmd1') == '1 step\n', read(log + '.cmd1'))
check('Continue sent "2 continue"', read(log + '.cmd2') == '2 continue\n', read(log + '.cmd2'))
check('a breakpoint set while playing reaches the engine', read(log + '.bp2') == '2\n9 %s\n11 %s\n' % (CHUNK, CHUNK),
      read(log + '.bp2'))
check('and is saved', saved(project) == {CHUNK: [9, 11]}, saved(project))
if shot:
    check('the screenshot was written', os.path.exists(shot))

# An engine without the debugger (it never says so): no Debugger tab during Play either.
plain = os.path.join(work, 'plain-redux.sh')
with open(plain, 'w', newline='\n') as fh:
    fh.write('#!/bin/sh\nSPLASHEDIT_FAKE_REDUX=1 exec "%s"\n' % display)
os.chmod(plain, os.stat(plain).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
project = project_copy({CHUNK: [9]})
lines = run(project, ['--key', 'f5', '--wait', '120', '--click', '@tab_debugger', '--wait', '5'],
            env=dict(PLAY, SPLASHEDIT_REDUX=plain))
check('no Debugger tab with an engine that has no debugger', any('@tab_debugger: no such control drawn' in l for l in lines),
      [l for l in lines if 'tab_debugger' in l or l.startswith('play: ')])

print('%d failure%s' % (fails, '' if fails == 1 else 's'))
sys.exit(1 if fails else 0)
