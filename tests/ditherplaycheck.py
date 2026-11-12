# Play with 1 console reads its pcsx-redux settings from a directory the
# editor writes (-portable), not from the user's own pcsx.json, and the Game
# view's Dither toggle (@game.dither) sets emulator.Dither there (#345).
# Play runs a fake pcsx-redux (SPLASHEDIT_REDUX, a shell script) that logs
# its arguments and the pcsx.json in its -portable directory, then execs
# emulator_tests (beside the editor) to show one 320x240 frame. HOME holds a
# global pcsx.json with Dither 0, as the reporter's did; it must not matter.
# Redux reads Dither once, at startup, so a toggle takes effect on the next
# Play: dithering on by default, then off after the toggle and a restart,
# then off again from the saved setting (--play-settings) in a new editor.
# usage: python3 ditherplaycheck.py <splashedit>   (needs a display: xvfb-run -a)
import json, os, shutil, stat, subprocess, sys, tempfile

editor = os.path.abspath(sys.argv[1])
display = os.path.join(os.path.dirname(editor), 'emulator_tests')
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
fails = 0
work = tempfile.mkdtemp()
engine = os.path.join(work, 'psxsplash.ps-exe')
with open(engine, 'wb') as fh:
    fh.write(b'PS-X EXE fake')
home = os.path.join(work, 'home')
for d in (os.path.join(home, '.config', 'pcsx-redux'), os.path.join(home, 'AppData', 'Roaming', 'pcsx-redux')):
    os.makedirs(d)
    with open(os.path.join(d, 'pcsx.json'), 'w') as fh:
        fh.write('{"emulator":{"Dither":0}}\n')
tmp = os.path.join(work, 'tmp')
os.makedirs(tmp)
log = os.path.join(work, 'launches.log')
redux = os.path.join(work, 'fake-redux.sh')
with open(redux, 'w', newline='\n') as fh:
    fh.write('#!/bin/sh\n'
             'echo launch >> "%s"\n'
             'prev=\n'
             'for a in "$@"; do\n'
             '  echo "arg $a" >> "%s"\n'
             '  if [ "$prev" = "-portable" ]; then echo "json $(tr -d \'\\n\' < "$a/pcsx.json")" >> "%s"; fi\n'
             '  prev=$a\n'
             'done\n'
             'SPLASHEDIT_FAKE_REDUX=320x240 exec "%s"\n' % (log, log, log, display))
os.chmod(redux, os.stat(redux).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def launches():
    """Each pcsx-redux start: (its arguments, its -portable pcsx.json or None)."""
    out = []
    if not os.path.exists(log):
        return out
    for l in open(log).read().splitlines():
        if l == 'launch':
            out.append(([], None))
        elif l.startswith('arg ') and out:
            out[-1][0].append(l[4:])
        elif l.startswith('json ') and out:
            out[-1] = (out[-1][0], json.loads(l[5:]))
    return out


def setting(path, key):
    if not os.path.exists(path):
        return None
    for l in open(path, encoding='utf-8').read().splitlines():
        if l.startswith(key + '='):
            return l[len(key) + 1:]
    return None


def run(args, settings):
    if os.path.exists(log):
        os.remove(log)
    project = os.path.join(tempfile.mkdtemp(), 'p')
    shutil.copytree(example, project)
    cmd = [editor, '--size', '1600x960', '--project', project, '--scene', os.path.join(project, 'courtyard.scene'),
           '--play-settings', settings, '--wait', '10', '--key', 'f5', '--wait', '240'] + args + \
          ['--wait', '20', '--screenshot', os.path.join(project, 'shot.png')]
    env = dict(os.environ, SPLASHEDIT_REDUX=redux, SPLASHEDIT_PSXSPLASH=engine, HOME=home,
               XDG_CONFIG_HOME=os.path.join(home, '.config'), APPDATA=os.path.join(home, 'AppData', 'Roaming'),
               TMPDIR=tmp, TMP=tmp, TEMP=tmp)
    r = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=300)
    lines = (r.stdout + r.stderr).strip().splitlines()
    missing = [l for l in lines if 'no such control drawn' in l]
    check('every control the script names was drawn (%s)' % ' '.join(args), not missing, missing)
    return lines, launches()


def dither(launch):
    j = launch[1]
    return j.get('emulator', {}).get('Dither') if isinstance(j, dict) else None


def portable(launch):
    a = launch[0]
    return a[a.index('-portable') + 1] if '-portable' in a and a.index('-portable') + 1 < len(a) else None


console = os.path.join(tmp, 'splashedit-play', 'console1')
cfg = os.path.join(work, 'play.cfg')
# Default on; the toggle turns it off, saved, and the next Play starts without it.
lines, ls = run(['--click', '@game.dither', '--wait', '5', '--key', 'f5', '--wait', '30', '--key', 'f5', '--wait', '240'], cfg)
check('Play started pcsx-redux twice', len(ls) == 2, [l[0] for l in ls])
if ls:
    first = ls[0]
    check('single-player Play passes -portable', '-portable' in first[0], first[0])
    check('its settings directory is the editor\'s, console1 under the Play directory',
          portable(first) and os.path.normpath(portable(first)) == os.path.normpath(console), (portable(first), console))
    check('the editor wrote pcsx.json there', first[1] is not None, first)
    check('dithering on by default (emulator.Dither 1), whatever the global pcsx.json says', dither(first) == 1, first[1])
    check('the console\'s link stays off', isinstance(first[1], dict) and
          first[1].get('emulator', {}).get('Debug', {}).get('SIO1Server') is False, first[1])
    check('the usual flags stay', all(f in first[0] for f in ('-run', '-fastboot', '-no-ui', '-shmdisplay', '-loadexe',
                                                                '-pcdrv', '-pcdrvbase')), first[0])
if len(ls) >= 2:
    check('after the toggle, the next Play writes emulator.Dither 0', dither(ls[1]) == 0, ls[1][1])
    check('and still passes -portable', '-portable' in ls[1][0], ls[1][0])
check('the toggle is saved off', setting(cfg, 'game_dither') == '0', open(cfg).read() if os.path.exists(cfg) else 'no file')
dl = [l for l in lines if l.startswith('game: dither ')]
check('the display bar reports dithering off', dl and dl[-1] == 'game: dither off', dl[-1:])
onDisk = os.path.join(console, 'pcsx.json')
check('the file on disk says Dither 0', os.path.exists(onDisk) and json.load(open(onDisk)).get('emulator', {}).get('Dither') == 0,
      open(onDisk).read() if os.path.exists(onDisk) else 'no ' + onDisk)

# A new editor on the saved settings: off from its first Play.
lines, ls = run([], cfg)
check('the saved setting reaches the first Play: emulator.Dither 0', ls and dither(ls[0]) == 0, ls[:1])

# And the toggle turns it back on.
lines, ls = run(['--click', '@game.dither', '--wait', '5', '--key', 'f5', '--wait', '30', '--key', 'f5', '--wait', '240'], cfg)
check('toggled back on, the next Play writes emulator.Dither 1', len(ls) == 2 and dither(ls[1]) == 1, ls)
check('the toggle is saved on', setting(cfg, 'game_dither') == '1')

print('%d failure%s' % (fails, '' if fails == 1 else 's'))
sys.exit(1 if fails else 0)
