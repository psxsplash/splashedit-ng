# Right-click menus in a short window, driven through the GUI: a menu taller
# than the window stays inside it and shows a thumb, and dragging the thumb
# brings its last row into view, as the title-bar menus do (#378):
#  - the Project panel's menu for a mesh (its last row Copy path);
#  - the Scene tree's menu for an object (its last row Pin in Inspector);
#  - the Project panel's type filter (its last row Other files).
# usage: python3 contextmenuscrollcheck.py <splashedit>   (needs a display: xvfb-run -a)
import os, re, shutil, subprocess, sys, tempfile

editor = os.path.abspath(sys.argv[1])
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
fails = 0
work = tempfile.mkdtemp()
settings = os.path.join(work, 'prefs', 'play.cfg')
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux')

# At 1000x170: "meshes" in the Project panel's folder tree, then the first
# file in its grid (a mesh) once that folder is open.
SHORT = '1000x170'
MESHES_ROW = '350,92'
MESH_TILE = '530,85'
# At 1000x300: the "Floor" row in the Scene tree.
TREE = '1000x300'
FLOOR_ROW = '98,179'
# At 1000x240 the Project panel's header is clear of the title bar (at 170 it
# is not) and its type filter's list still does not fit.
TYPES = '1000x240'


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def run(size, args, shot):
    """Runs the editor at `size` on a fresh courtyard copy, then `args`, and
    returns the controls drawn in the last frame {name: (x0, y0, x1, y1)}."""
    project = os.path.join(tempfile.mkdtemp(), 'courtyard')
    shutil.copytree(example, project)
    cmd = [editor, '--size', size, '--project', project, '--play-settings', settings, '--wait', '5'] + args + \
        ['--wait', '5', '--screenshot', os.path.join(work, shot)]
    r = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=180)
    lines = (r.stdout + r.stderr).strip().splitlines()
    missing = [l for l in lines if 'no such control drawn' in l]
    check(size + ': every control the script names was drawn', not missing, missing)
    rects = {}
    for l in lines:
        m = re.match(r'control (.+) at (-?\d+),(-?\d+) (-?\d+),(-?\d+)$', l)
        if m:
            rects[m.group(1)] = tuple(int(v) for v in m.groups()[1:])
    return rects


def inside(rect, box):
    return bool(rect and box) and rect[1] >= box[1] and rect[3] <= box[3]


def menu_checks(label, size, height, open_args, menu, last, shot):
    names = lambda c: sorted(k for k in c if k.startswith(menu))
    c = run(size, open_args, shot + '-open.png')
    box = c.get(menu + '#popup')
    check(label + ' opens', box is not None, names(c))
    check(label + ' stays inside the window', box and box[1] >= 0 and box[3] <= height, box)
    check(label + ' shows a thumb', menu + '/thumb' in c, names(c))
    check(label + ': ' + last + ', at the bottom, is not wholly in view yet', not inside(c.get(menu + '/' + last), box),
          (c.get(menu + '/' + last), box))
    c = run(size, open_args + ['--drag', '@' + menu + '/thumb,0,0,0,300', '--wait', '3'], shot + '-dragged.png')
    row, box = c.get(menu + '/' + last), c.get(menu + '#popup')
    check(label + ': the dragged thumb shows ' + last + ' inside the menu', inside(row, box) and box[3] <= height, (row, box))


menu_checks('the Project menu for a mesh', SHORT, 170,
            ['--click', MESHES_ROW, '--wait', '3', '--click', MESH_TILE + ',1', '--wait', '3'], 'assets.menu', 'Copy path', 'assets')
menu_checks("the Scene tree's menu", TREE, 300, ['--click', FLOOR_ROW + ',1', '--wait', '3'], 'treemenu', 'Pin in Inspector', 'tree')
menu_checks("the Project panel's type filter", TYPES, 240, ['--click', '@project.typefilter', '--wait', '3'], 'project.types',
            'Other files', 'types')

print('screenshots in ' + work)
print('%d failure%s' % (fails, '' if fails == 1 else 's'))
sys.exit(1 if fails else 0)
