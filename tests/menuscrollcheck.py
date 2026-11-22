# Long menus and the Add object picker in a short window (#378), driven
# through the GUI:
#  - a title-bar menu longer than the window stops at its bottom and shows a
#    thumb; dragging the thumb brings its last row into view;
#  - View > Panels and View > Timeline are submenus: the View menu lists
#    them, not their rows, and Panels opens its rows beside it;
#  - the Add object picker fills a tall window, and in a short one shows a
#    thumb that scrolls it.
# usage: python3 menuscrollcheck.py <splashedit>   (needs a display: xvfb-run -a)
import os, re, shutil, subprocess, sys, tempfile

editor = os.path.abspath(sys.argv[1])
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
fails = 0
work = tempfile.mkdtemp()
settings = os.path.join(work, 'prefs', 'play.cfg')
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux')


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def run(size, args):
    """Runs the editor at `size` on a fresh courtyard copy, then `args`, and
    returns the controls drawn in the last frame {name: (x0, y0, x1, y1)}."""
    project = os.path.join(tempfile.mkdtemp(), 'courtyard')
    shutil.copytree(example, project)
    cmd = [editor, '--size', size, '--project', project, '--play-settings', settings, '--wait', '5'] + args + \
        ['--wait', '5', '--screenshot', os.path.join(project, 'shot.png')]
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


VIEW = ['--click', '@menu.view', '--wait', '3']

# View in a tall window: Panels and Timeline are rows of their own, no thumb.
c = run('1600x960', VIEW)
check('View lists Panels and Timeline', 'menu.view/Panels' in c and 'menu.view/Timeline' in c, sorted(k for k in c if k.startswith('menu.')))
check("View does not list the panels' rows", 'menu.view/Project' not in c and 'menu.view/Console' not in c, sorted(c))
check('View fits: no thumb', 'menu.view/thumb' not in c, c.get('menu.view/thumb'))

# Panels opens beside the View menu with its rows.
c = run('1600x960', VIEW + ['--click', '@menu.view/Panels', '--wait', '3'])
sub, menu = c.get('menu.view/Panels#popup'), c.get('menu.view#popup')
check('Panels opens a submenu right of the View menu', sub and menu and sub[0] >= menu[2], (sub, menu))
check('Panels lists the panels', all('menu.view/Panels/' + k in c for k in ('Project', 'Console', 'Strings', 'Frame Debugger')),
      sorted(k for k in c if k.startswith('menu.view/Panels/')))

# View in a short window: cut at the window's bottom, with a thumb.
c = run('900x360', VIEW)
menu = c.get('menu.view#popup')
check('View stops at the window bottom', menu and menu[3] <= 360, menu)
check('View shows a thumb', 'menu.view/thumb' in c, sorted(k for k in c if k.startswith('menu.view')))
check('Timeline, past the bottom, is not clickable yet', 'menu.view/Timeline' not in c, c.get('menu.view/Timeline'))

# Dragging the thumb down brings the last row, Timeline, into view.
c = run('900x360', VIEW + ['--drag', '@menu.view/thumb,0,0,0,300', '--wait', '3'])
row, menu = c.get('menu.view/Timeline'), c.get('menu.view#popup')
check('the dragged thumb shows Timeline inside the menu', row and menu and row[1] >= menu[1] and row[3] <= menu[3], (row, menu))

# Add object: every preset in a tall window, a thumb in a short one.
c = run('1600x960', ['--key', 'ctrl+a', '--wait', '3'])
rows = [k for k in c if k.startswith('##addobject/') and k != '##addobject/thumb']
check('Add object shows more than 10 rows in a tall window', len(rows) > 10, len(rows))
c = run('1024x600', ['--key', 'ctrl+a', '--wait', '3'])
check('Add object shows a thumb in a short window', '##addobject/thumb' in c, sorted(k for k in c if k.startswith('##addobject')))
check('Path, the last preset, is not shown yet', '##addobject/Path' not in c, c.get('##addobject/Path'))
c = run('1024x600', ['--key', 'ctrl+a', '--wait', '3', '--drag', '@##addobject/thumb,0,0,0,300', '--wait', '3'])
check('dragging the thumb shows Path', '##addobject/Path' in c, sorted(k for k in c if k.startswith('##addobject')))

print('%d failure%s' % (fails, '' if fails == 1 else 's'))
sys.exit(1 if fails else 0)
