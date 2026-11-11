# The project-level File menu and the Project settings tab (#343), driven
# through the GUI on copies of the courtyard example:
# - the inspector shows the scene's own settings only; the build list, VRAM
#   layout and disc live in its Project settings tab;
# - File > New Project refuses a folder that is a project already (one holding
#   a .scene, editor::isProject) and leaves it as it was;
# - File > New Scene writes an empty <name>.scene beside the project's scenes,
#   adds it to scenes.list (after the open scene when the list was empty) and
#   refuses a name that is taken;
# - File > Open Project opens a project folder's start scene and refuses a
#   folder that is no project; the project joins File > Recent projects;
# - Export offers <disc volume>.bin, the project folder's name when the volume
#   is empty, either cleaned for a file system.
# Dialogs are answered through SPLASHEDIT_FILE_DIALOG (hold or pick:<path>).
# usage: python3 projectmenucheck.py <splashedit>   (needs a display: xvfb-run -a)
import json, os, shutil, subprocess, sys, tempfile

editor = os.path.abspath(sys.argv[1])
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
fails = 0
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def project(name='proj'):
    """A fresh copy of the courtyard, in a folder of its own, named `name`."""
    p = os.path.join(tempfile.mkdtemp(), name)
    shutil.copytree(example, p)
    return p


def run(proj, args, env=None, extra=()):
    """Runs the editor on `proj`, then `args`; returns the output lines."""
    cmd = [editor, '--size', '1600x960', '--project', proj] + list(extra) + ['--wait', '5'] + args + [
        '--wait', '5', '--screenshot', os.path.join(os.path.dirname(proj), 'shot.png')]
    r = subprocess.run(cmd, capture_output=True, text=True, env=dict(ENV, **(env or {})), timeout=180)
    lines = (r.stdout + r.stderr).strip().splitlines()
    # A control the script named and the editor never drew is a broken check, not a pass.
    missing = [l for l in lines if 'no such control drawn' in l]
    check('every control the script names was drawn', not missing, missing)
    return lines


def click(name):
    return ['--click', '@' + name, '--wait', '3']


def typed(field, text):
    return click(field) + ['--text', text, '--wait', '3']


def controls(lines):
    return {l.split(' at ')[0][len('control '):] for l in lines if l.startswith('control ')}


def tagged(lines, tag):
    return [l for l in lines if l.startswith(tag)]


def listed(proj):
    f = os.path.join(proj, 'scenes.list')
    return open(f, encoding='utf-8').read() if os.path.exists(f) else None


FILE = click('menu.file')
PROJECT_ONLY = ('build.', 'vram.', 'disc.', 's_build', 's_vram', 's_disc')

# The inspector: the scene's own settings; the project's are one tab away.
p = project()
lines = run(p, [])
have = controls(lines)
check('the scene inspector draws no project setting', not [c for c in have if c.startswith(PROJECT_ONLY)],
      sorted(c for c in have if c.startswith(PROJECT_ONLY)))
check('the scene inspector keeps the scene settings', {'s_rendering', 's_render', 'scene.panel'} <= have,
      sorted(c for c in have if c.startswith('s_')))
lines = run(p, click('inspector.tab/Project settings'))
have = controls(lines)
check('the Project settings tab holds the build list, VRAM and disc',
      {'s_build', 's_vram', 's_disc', 'build.add', 'disc.volume', 'project.strings', 'project.panel'} <= have,
      sorted(c for c in have if c.startswith(PROJECT_ONLY + ('project.',))))
check('the tab says it is showing', 'inspector: tab project' in lines, tagged(lines, 'inspector:'))
lines = run(p, click('inspector.tab/Project settings'), extra=['--select', 'Crate'])
check('it shows whatever is selected', 's_build' in controls(lines) and 'obj.panel' not in controls(lines),
      sorted(c for c in controls(lines) if c.startswith(('s_', 'obj.'))))
lines = run(p, click('inspector.tab/Project settings') + click('tree.Crate'))
check('picking an object brings the Inspector tab back',
      tagged(lines, 'inspector:')[-1:] == ['inspector: tab inspector'] and 'obj.panel' in controls(lines), tagged(lines, 'inspector:'))

# New Project refuses a folder that is already a project.
p = project('taken')
before = sorted(os.listdir(p))
lines = run(p, FILE + click('menu.file/New Project (New Folder)...') + click('newproj.tmpl.empty') +
            typed('newproj.name', 'taken') + typed('newproj.location', os.path.dirname(p)) + click('newproj.create'))
out = tagged(lines, 'newproject:')
check('New Project refuses a folder that is a project', len(out) == 1 and out[0].startswith('newproject: failed ') and
      'already holds a project (courtyard.scene)' in out[0] and 'Open Project' in out[0], out)
check('the refused folder is left as it was', sorted(os.listdir(p)) == before, (before, sorted(os.listdir(p))))
check('the refused modal stays open', 'popups open: yes' in lines, tagged(lines, 'popups'))

# New Scene: an empty scene beside the others, added to the build list.
p = project()
lines = run(p, FILE + click('menu.file/New Scene in Project...') + typed('newscene.name', 'level2') + click('newscene.create'))
out = tagged(lines, 'newscene:')
check('New Scene creates level2.scene', out == ['newscene: created level2.scene'], out)
scene = os.path.join(p, 'level2.scene')
check('level2.scene is written in the project folder', os.path.isfile(scene))
if os.path.isfile(scene):
    check('the new scene is empty', not json.load(open(scene, encoding='utf-8')).get('objects'), open(scene).read()[:200])
check('scenes.list holds the open scene, then the new one', listed(p) == 'courtyard.scene\nlevel2.scene\n', repr(listed(p)))
lines = run(p, FILE + click('menu.file/New Scene in Project...') + typed('newscene.name', 'level2') + click('newscene.create'))
out = tagged(lines, 'newscene:')
check('a taken scene name is refused', len(out) == 1 and out[0].startswith('newscene: failed ') and 'already exists' in out[0], out)
check('the refusal leaves scenes.list', listed(p) == 'courtyard.scene\nlevel2.scene\n', repr(listed(p)))

# Open Project: a project folder opens its start scene; a plain folder is refused.
p, other = project(), project('other')
os.rename(os.path.join(other, 'courtyard.scene'), os.path.join(other, 'yard.scene'))
lines = run(p, ['--key', 'ctrl+shift+o', '--wait', '5', '--click', '@menu.file', '--wait', '3'], env={'SPLASHEDIT_FILE_DIALOG': 'pick:' + other})
check('Open Project opens the folder\'s scene', 'openproject: opened other yard.scene' in lines, tagged(lines, 'openproject:'))
check('the project joins Recent projects', 'menu.file/other' in controls(lines),
      sorted(c for c in controls(lines) if c.startswith('menu.file/')))
plain = tempfile.mkdtemp()
lines = run(p, ['--key', 'ctrl+shift+o', '--wait', '5'], env={'SPLASHEDIT_FILE_DIALOG': 'pick:' + plain})
out = tagged(lines, 'openproject:')
check('a folder with no scene is refused', len(out) == 1 and out[0].startswith('openproject: failed ') and
      'not a project' in out[0], out)

# Export's file name: the disc volume, else the project folder's name.
engine = os.path.abspath(sys.argv[0])  # any file: the dialog is held, nothing is built
EXPORT = ['--wait', '10', '--key', 'ctrl+b', '--wait', '5']
DIALOG = {'SPLASHEDIT_FILE_DIALOG': 'hold', 'SPLASHEDIT_DISC_ENGINE': engine}
p = project()
json.dump({'volume': 'MY_GAME'}, open(os.path.join(p, 'disc.json'), 'w'))
lines = run(p, EXPORT, env=DIALOG)
want = 'file dialog: export in ' + os.path.join(p, 'MY_GAME.bin')
check('Export offers the disc volume as the file name', want in lines, tagged(lines, 'file dialog:'))
p = project('my proj')
lines = run(p, EXPORT, env=DIALOG)
want = 'file dialog: export in ' + os.path.join(p, 'my proj.bin')
check('with no volume, Export offers the project folder\'s name', want in lines, tagged(lines, 'file dialog:'))
p = project('bad:name?')
json.dump({'volume': ''}, open(os.path.join(p, 'disc.json'), 'w'))
lines = run(p, EXPORT, env=DIALOG)
want = 'file dialog: export in ' + os.path.join(p, 'badname.bin')
check('the offered name is cleaned for a file system', want in lines, tagged(lines, 'file dialog:'))

print('%d failure%s' % (fails, '' if fails == 1 else 's'))
sys.exit(1 if fails else 0)
