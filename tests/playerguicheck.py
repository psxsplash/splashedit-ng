# Build > Export Standalone Player... in the editor: off with no pcsx-redux
# (its row's click does nothing), and with a stand-in pcsx-redux it writes
# the player where --export-player-to says, the status-bar notice's Show in
# folder link revealing the launcher. The folder's contents are
# playercheck.py's business; here only that the editor writes one, and that
# it follows the project's player.json (#347): the game name, the launcher's
# name, the pcsx.json it starts with (filtering off and fast boot on by
# default, a 640 x 480 window, the project's own pcsx.json over them), and
# that the Project settings tab's Standalone player section edits it.
# usage: python3 playerguicheck.py <splashedit>   (needs a display: xvfb-run -a)
import json, os, shutil, subprocess, sys, tempfile

editor = os.path.abspath(sys.argv[1])
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
tmp = tempfile.mkdtemp()
fails = 0
windows, mac = sys.platform == 'win32', sys.platform == 'darwin'


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


engine = os.path.join(tmp, 'cd.ps-exe')
open(engine, 'wb').write(b'PS-X EXE' + bytes(2040) + b'code' * 300 + b'SCENE%d/SCENE_%d.SPK;1\0' + bytes(100))
fake = os.path.join(tmp, 'fake', 'pcsx-redux.exe' if windows else 'pcsx-redux')
os.makedirs(os.path.dirname(fake))
open(fake, 'wb').write(b'stand-in')
ext = '.bat' if windows else '.sh'  # a lone file: the launcher of the system this runs on
if mac:
    ext = '.command'

project = os.path.join(tmp, 'courtyard')
shutil.copytree(example, project)
item = '@menu.build/Export Standalone Player...'


def run(redux, out, before=()):
    env = dict(os.environ, SPLASHEDIT_REDUX=redux, SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe',
               SPLASHEDIT_DISC_ENGINE=engine)
    cmd = [editor, '--size', '1600x960', '--project', project, '--export-player-to', out, '--wait', '30'] + list(before) + [
           '--click', '@menu.build', '--wait', '3', '--click', item, '--wait-play', '--wait', '5',
           '--click', '@notice.reveal', '--wait', '3', '--screenshot', os.path.join(tmp, 'shot.png')]
    r = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=120)
    return r, (r.stdout + r.stderr).strip()


off = os.path.join(tmp, 'off')
r, out = run(os.path.join(tmp, 'nothing', 'pcsx-redux'), off)
check('no pcsx-redux: the item is off, nothing built or written', r.returncode == 0 and not os.path.exists(off) and
      'player: ' not in r.stdout and 'build report: Export Standalone Player' not in r.stdout, out[-300:])

player = os.path.join(tmp, 'player')
r, out = run(fake, player)
launcher = os.path.join(player, 'courtyard' + ext)
check('with pcsx-redux it writes the player, named after the project', r.returncode == 0 and os.path.isfile(launcher) and
      os.path.isfile(os.path.join(player, 'game', 'courtyard.bin')) and
      os.path.isfile(os.path.join(player, 'redux', os.path.basename(fake))), out[-300:])
check('it says where', 'player: ' + launcher in r.stdout.splitlines(), out[-300:])
check('Show in folder reveals the launcher', 'reveal: ' + launcher in r.stdout.splitlines(),
      [l for l in r.stdout.splitlines() if l.startswith('reveal')])
lines = [l for l in r.stdout.splitlines() if l.startswith('build report: Export Standalone Player')]
check('the build report has the lone-copy warning', len(lines) == 1 and lines[0].endswith('errors 0 warnings 1'), lines)

def config(folder):
    with open(os.path.join(folder, 'config', 'pcsx.json')) as f:
        return json.load(f)


c = config(player)
check('its pcsx.json: no linear filtering, fast boot, a 2x window', c['emulator']['LinearFiltering'] is False and
      c['emulator']['FastBoot'] is True and c['gui']['WindowSizeX'] == 640 and c['gui']['WindowSizeY'] == 480 and
      c['emulator']['AutoUpdate'] is False, c)
# The window: titled with the folder's name; no icon image of its own, so pcsx-redux's.
check('its window: the project\'s name, no icon file', c['gui'].get('WindowTitle') == 'courtyard' and
      'WindowIcon' not in c['gui'] and not os.path.exists(os.path.join(player, 'config', 'window-icon.png')), c)

# player.json: the game's name, the launcher's, the project's own pcsx.json.
os.makedirs(os.path.join(project, 'player'))
with open(os.path.join(project, 'player', 'pcsx.json'), 'w') as f:
    json.dump({'emulator': {'Dither': 0}, 'gui': {'WindowSizeX': 1280, 'WindowSizeY': 960}}, f)
with open(os.path.join(project, 'player.json'), 'w') as f:
    json.dump({'name': 'Lamp Night', 'exe': 'night', 'pcsxJson': 'player/pcsx.json'}, f)
named = os.path.join(tmp, 'named')
# The Standalone player section, scrolled to, its Fast boot switched off, before the export.
tab = ['--click', '@inspector.tab/Project settings', '--wait', '5', '--scroll-to', '@player.playerfastboot', '--wait', '5',
       '--click', '@player.playerfastboot', '--wait', '5']
r, out = run(fake, named, tab)
check('the launcher has the project\'s name for it', os.path.isfile(os.path.join(named, 'night' + ext)) and
      not os.path.exists(os.path.join(named, 'courtyard' + ext)), sorted(os.listdir(named)) if os.path.isdir(named) else out[-300:])
check('the disc has the game name', os.path.isfile(os.path.join(named, 'game', 'Lamp Night.bin')),
      sorted(os.listdir(os.path.join(named, 'game'))) if os.path.isdir(os.path.join(named, 'game')) else out[-300:])
with open(os.path.join(project, 'player.json')) as f:
    saved = json.load(f)
check('the Fast boot switch saved player.json', saved == {'name': 'Lamp Night', 'exe': 'night', 'pcsxJson': 'player/pcsx.json',
                                                        'fastBoot': False}, saved)
check('the section says so', 'player settings: name Lamp Night exe night linear off fastboot off scale 2' in r.stdout.splitlines(),
      [l for l in r.stdout.splitlines() if l.startswith('player settings')])
c = config(named) if os.path.isdir(os.path.join(named, 'config')) else {}
check('its pcsx.json: fast boot off, the project\'s keys over the rest',
      c.get('emulator', {}).get('FastBoot') is False and c['emulator'].get('Dither') == 0 and
      c['emulator'].get('LinearFiltering') is False and c.get('gui') == {'WindowSizeX': 1280, 'WindowSizeY': 960,
                                                                  'WindowTitle': 'Lamp Night'}, c)

# A pcsx.json that is not JSON: the export fails and says why.
with open(os.path.join(project, 'player', 'pcsx.json'), 'w') as f:
    f.write('{oops')
r, out = run(fake, os.path.join(tmp, 'bad'))
check('a broken pcsx.json fails the export', not os.path.exists(os.path.join(tmp, 'bad', 'night' + ext)) and
      'player/pcsx.json is not used: it is not valid JSON' in out, out[-400:])

print(f'{fails} failures')
shutil.rmtree(tmp, ignore_errors=True)
sys.exit(1 if fails else 0)
