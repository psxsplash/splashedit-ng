# Build > Export for the Web... in the editor: off with no pcsx-redux web
# build set (its row's click does nothing), and with a stand-in one it writes
# the web export where --export-web-to says, the status-bar notice's Show in
# folder link revealing its index.html. What the page holds is editor_tests'
# business (testWebExport); here only that the editor writes one.
# usage: python3 webguicheck.py <splashedit>   (needs a display: xvfb-run -a)
import os, shutil, subprocess, sys, tempfile

editor = os.path.abspath(sys.argv[1])
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
tmp = tempfile.mkdtemp()
fails = 0


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


engine = os.path.join(tmp, 'cd.ps-exe')
open(engine, 'wb').write(b'PS-X EXE' + bytes(2040) + b'code' * 300 + b'SCENE%d/SCENE_%d.SPK;1\0' + bytes(100))
web = os.path.join(tmp, 'redux-web')
os.makedirs(web)
for f in ('pcsx-redux.js', 'pcsx-redux.wasm', 'pcsx-redux.data', 'enable-threads.js'):
    open(os.path.join(web, f), 'wb').write(b'stand-in')

project = os.path.join(tmp, 'courtyard')
shutil.copytree(example, project)
item = '@menu.build/Export for the Web...'


def run(redux_web, out):
    env = dict(os.environ, SPLASHEDIT_REDUX_WEB=redux_web, SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe',
               SPLASHEDIT_DISC_ENGINE=engine)
    cmd = [editor, '--size', '1600x960', '--project', project, '--export-web-to', out, '--wait', '30',
           '--click', '@menu.build', '--wait', '3', '--click', item, '--wait-play', '--wait', '5',
           '--click', '@notice.reveal', '--wait', '3', '--screenshot', os.path.join(tmp, 'shot.png')]
    r = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=120)
    return r, (r.stdout + r.stderr).strip()


off = os.path.join(tmp, 'off')
r, out = run('', off)
check('no web build: the item is off, nothing built or written', r.returncode == 0 and not os.path.exists(off) and
      'web: ' not in r.stdout and 'build report: Export for the Web' not in r.stdout, out[-300:])

dest = os.path.join(tmp, 'web-out')
r, out = run(web, dest)
page = os.path.join(dest, 'index.html')
check('with a web build it writes the export', r.returncode == 0 and os.path.isfile(page) and
      os.path.isfile(os.path.join(dest, 'game', 'courtyard.bin')) and
      os.path.isfile(os.path.join(dest, 'pcsx-redux.wasm')), out[-300:])
check('the page is titled after the project', os.path.isfile(page) and '<title>courtyard</title>' in open(page).read())
check('it says where', 'web: ' + page in r.stdout.splitlines(), out[-300:])
check('Show in folder reveals the page', 'reveal: ' + page in r.stdout.splitlines(),
      [l for l in r.stdout.splitlines() if l.startswith('reveal')])
lines = [l for l in r.stdout.splitlines() if l.startswith('build report: Export for the Web')]
check('the build report has no errors', len(lines) == 1 and 'errors 0' in lines[0], lines)

print(f'{fails} failures')
shutil.rmtree(tmp, ignore_errors=True)
sys.exit(1 if fails else 0)
