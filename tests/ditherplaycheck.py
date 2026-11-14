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
# The memory cards outlive the Play directory: the pcsx.json names them
# (emulator.Mcd1 / Mcd2, absolute) under the preferences directory
# (--prefs-dir, as SDL_GetPrefPath outside screenshot runs), in
# play/<16 hex digits for the project>/console1, beside a project.txt naming
# the project. The fake redux treats them as redux's MemoryCard::loadMcd and
# saveMcd do (a relative path under -portable, created when missing, then a
# save written in place), so a second Play, and a new editor after the temp
# directory is cleaned, find the same card file with the earlier save in it.
# Without a usable preferences directory the cards stay in the Play
# directory as before, and Play still starts.
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
prefs = os.path.join(work, 'prefs')
cardsTool = os.path.join(work, 'fake-cards.py')
with open(cardsTool, 'w') as fh:
    fh.write(r'''import json, os, sys
portable, log = sys.argv[1], sys.argv[2]
emu = json.load(open(os.path.join(portable, 'pcsx.json'))).get('emulator', {})
with open(log, 'a') as out:
    for n in (1, 2):
        path = emu.get('Mcd%d' % n) or 'memcard%d.mcd' % n
        if not os.path.isabs(path):
            path = os.path.join(portable, path)
        created = not os.path.exists(path)
        if created:
            with open(path, 'wb') as f:
                f.write(bytes(128 * 1024))
        with open(path, 'r+b') as f:
            f.seek(0x2000)
            saves = f.read(1)[0]
            f.seek(0x2000)
            f.write(bytes([saves + 1]))
        st = os.stat(path)
        out.write('card %d %s %s %d %d %d\n' % (n, 'created' if created else 'kept', path, st.st_ino, st.st_size, saves))
''')
redux = os.path.join(work, 'fake-redux.sh')
with open(redux, 'w', newline='\n') as fh:
    fh.write('#!/bin/sh\n'
             'echo launch >> "%s"\n'
             'prev=\n'
             'for a in "$@"; do\n'
             '  echo "arg $a" >> "%s"\n'
             '  if [ "$prev" = "-portable" ]; then echo "json $(tr -d \'\\n\' < "$a/pcsx.json")" >> "%s";'
             ' "%s" -I "%s" "$a" "%s"; fi\n'
             '  prev=$a\n'
             'done\n'
             'SPLASHEDIT_FAKE_REDUX=320x240 exec "%s"\n' % (log, log, log, sys.executable, cardsTool, log, display))
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
            out.append(([], None, {}))
        elif l.startswith('arg ') and out:
            out[-1][0].append(l[4:])
        elif l.startswith('json ') and out:
            out[-1] = (out[-1][0], json.loads(l[5:]), out[-1][2])
        elif l.startswith('card ') and out:
            n, how, rest = l[5:].split(' ', 2)
            path, ino, size, saves = rest.rsplit(' ', 3)
            out[-1][2][int(n)] = dict(how=how, path=path, ino=int(ino), size=int(size), saves=int(saves))
    return out


def setting(path, key):
    if not os.path.exists(path):
        return None
    for l in open(path, encoding='utf-8').read().splitlines():
        if l.startswith(key + '='):
            return l[len(key) + 1:]
    return None


def newProject():
    project = os.path.join(tempfile.mkdtemp(), 'p')
    shutil.copytree(example, project)
    return project


def run(args, settings, project=None, prefsDir=prefs):
    if os.path.exists(log):
        os.remove(log)
    project = project or newProject()
    cmd = [editor, '--size', '1600x960', '--project', project, '--scene', os.path.join(project, 'courtyard.scene'),
           '--play-settings', settings] + (['--prefs-dir', prefsDir] if prefsDir else []) + \
          ['--wait', '10', '--key', 'f5', '--wait', '240'] + args + \
          ['--wait', '20', '--screenshot', os.path.join(project, 'shot.png')]
    env = dict(os.environ, SPLASHEDIT_REDUX=redux, SPLASHEDIT_PSXSPLASH=engine, HOME=home,
               XDG_CONFIG_HOME=os.path.join(home, '.config'), XDG_DATA_HOME=os.path.join(home, '.local', 'share'),
               APPDATA=os.path.join(home, 'AppData', 'Roaming'), TMPDIR=tmp, TMP=tmp, TEMP=tmp)
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


def under(path, root):
    return bool(path) and os.path.isabs(path) and os.path.commonpath([os.path.realpath(path), os.path.realpath(root)]) == \
        os.path.realpath(root)


def cardPaths(launch):
    j = launch[1].get('emulator', {}) if isinstance(launch[1], dict) else {}
    return j.get('Mcd1'), j.get('Mcd2')


console = os.path.join(tmp, 'splashedit-play', 'console1')
cfg = os.path.join(work, 'play.cfg')
project = newProject()
# Default on; the toggle turns it off, saved, and the next Play starts without it.
lines, ls = run(['--click', '@game.dither', '--wait', '5', '--key', 'f5', '--wait', '30', '--key', 'f5', '--wait', '240'], cfg,
                project)
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


# Memory cards: in the preferences directory, per project, kept between Plays.
cards = None
if ls:
    m1, m2 = cardPaths(ls[0])
    check('pcsx.json names memory card 1 (emulator.Mcd1), absolute, under the preferences directory',
          under(m1, os.path.join(prefs, 'play')), (m1, prefs))
    check('and memory card 2 (emulator.Mcd2) beside it', m1 and m2 and os.path.dirname(m2) == os.path.dirname(m1) and
          os.path.basename(m1) == 'memcard1.mcd' and os.path.basename(m2) == 'memcard2.mcd', (m1, m2))
    if m1:
        cards = os.path.dirname(os.path.dirname(m1))
        key = os.path.basename(cards)
        check('in play/<16 hex digits>/console1', os.path.basename(os.path.dirname(m1)) == 'console1' and len(key) == 16 and
              all(c in '0123456789abcdef' for c in key) and os.path.dirname(cards) == os.path.join(prefs, 'play'), m1)
        pt = os.path.join(cards, 'project.txt')
        check('project.txt there names the project', os.path.exists(pt) and
              os.path.realpath(open(pt, encoding='utf-8').read().strip()) == os.path.realpath(project),
              open(pt).read() if os.path.exists(pt) else 'no ' + pt)
    c = ls[0][2]
    check('the first Play creates the cards there', c.get(1, {}).get('how') == 'created' and c.get(1, {}).get('path') == m1 and
          c.get(2, {}).get('path') == m2, c)
    check('the cards are not in the Play directory', not os.path.exists(os.path.join(console, 'memcard1.mcd')))
if len(ls) >= 2:
    a, b = ls[0][2].get(1, {}), ls[1][2].get(1, {})
    check('the next Play names the same cards', cardPaths(ls[1]) == cardPaths(ls[0]), (cardPaths(ls[0]), cardPaths(ls[1])))
    check('and finds the same card file with the first one\'s save: not recreated', b.get('how') == 'kept' and
          b.get('ino') == a.get('ino') and b.get('size') == a.get('size') == 128 * 1024 and b.get('saves') == 1, (a, b))
first = ls[0][2].get(1, {}) if ls else {}

# A new editor on the saved settings: off from its first Play. The temp
# directory is cleaned first, as a reboot would: the cards stay.
shutil.rmtree(os.path.join(tmp, 'splashedit-play'), ignore_errors=True)
lines, ls = run([], cfg, project)
check('the saved setting reaches the first Play: emulator.Dither 0', ls and dither(ls[0]) == 0, ls[:1])
if ls:
    c = ls[0][2].get(1, {})
    check('a new editor, after the temp directory went, finds the same card with both saves', c.get('how') == 'kept' and
          c.get('ino') == first.get('ino') and c.get('saves') == 2 and c.get('path') == first.get('path'), (first, c))
    check('and reports where the cards are', 'play: memory cards ' + str(cards) in lines, [l for l in lines if 'memory cards' in l])

# Another project gets cards of its own.
lines, ls = run([], cfg)
if ls:
    m1, _ = cardPaths(ls[0])
    check('another project: its own cards under the preferences directory', under(m1, os.path.join(prefs, 'play')) and
          cards and not under(m1, cards) and ls[0][2].get(1, {}).get('how') == 'created', (m1, cards))

# No preferences directory, or one that cannot be written: as before, the
# cards in the Play directory, a line saying so, and Play still starts.
blocked = os.path.join(work, 'not-a-dir')
open(blocked, 'w').close()
for name, pd in (('no preferences directory', None), ('an unwritable preferences directory', blocked)):
    lines, ls = run([], cfg, project, pd)
    check(name + ': Play still starts pcsx-redux', len(ls) == 1, ls)
    if ls:
        check(name + ': no card paths in pcsx.json', cardPaths(ls[0]) == (None, None), ls[0][1])
        check(name + ': redux keeps them in the Play directory', ls[0][2].get(1, {}).get('path') ==
              os.path.join(console, 'memcard1.mcd'), ls[0][2])
    check(name + ': one line says so', len([l for l in lines if 'Play keeps memory cards in the temporary Play directory' in l]) == 1,
          [l for l in lines if 'memory cards' in l])

# And the toggle turns it back on.
lines, ls = run(['--click', '@game.dither', '--wait', '5', '--key', 'f5', '--wait', '30', '--key', 'f5', '--wait', '240'], cfg)
check('toggled back on, the next Play writes emulator.Dither 1', len(ls) == 2 and dither(ls[1]) == 1, ls)
check('the toggle is saved on', setting(cfg, 'game_dither') == '1')

print('%d failure%s' % (fails, '' if fails == 1 else 's'))
sys.exit(1 if fails else 0)
