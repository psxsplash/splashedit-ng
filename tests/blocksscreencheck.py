# Screen blocks in the block editor, driven through the GUI on copies of the
# courtyard example whose Game Logic runs scripts/door.blocks (the empty
# fixture blocks/editor/empty.blocks). The palette's Screen section must offer
# every Screen block; "fade screen to <colour> over N frames" goes under a
# "when the player interacts" hat, its colour field opens a colour picker and
# a click in the picker's square makes the colour red-ish (red near full, green and
# blue equal), which the saved file keeps as "#RRGGBB" and `splashpack-cli
# blocks` turns into Screen.FadeOut(r, g, b, 30) with the same numbers. The
# Lua panel's scripted line must count that Lua. A colour field that is not
# "#RRGGBB" is a Lua panel error naming the block. Last, blocks/screen.blocks
# opened with --shot <png> saves a picture of the Screen blocks.
# usage: python3 blocksscreencheck.py <splashedit> [<splashpack-cli>] [--shot <png>]   (needs a display: xvfb-run -a)
import json, os, re, shutil, subprocess, sys, tempfile

args = sys.argv[1:]
shot = None
if '--shot' in args:
    i = args.index('--shot')
    shot = os.path.abspath(args[i + 1])
    del args[i:i + 2]
editor = os.path.abspath(args[0])
cli = os.path.abspath(args[1]) if len(args) > 1 else os.path.join(os.path.dirname(editor), 'cli', 'splashpack-cli')
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
fixture = os.path.join(here, 'blocks', 'editor', 'empty.blocks')
fails = 0
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')
EDIT_SCRIPT = '1410,419'  # Game Logic's "Edit script" button at 1600x960 (as in blockseditorcheck.py)
OPEN = ['--select', 'Game Logic', '--click', EDIT_SCRIPT, '--wait', '10']
HAT_AT = (40, 40)  # where the hat is dropped, in canvas coordinates
SCREEN_OPS = ['fade_out', 'fade_in', 'flash', 'shake', 'letterbox', 'trail_on', 'trail_off', 'screen_fading']


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def project_copy(blocks=None):
    project = os.path.join(tempfile.mkdtemp(), 'courtyard')
    shutil.copytree(example, project)
    scene = os.path.join(project, 'courtyard.scene')
    s = json.load(open(scene, encoding='utf-8'))
    logic = [o for o in s['objects'] if o['name'] == 'Game Logic']
    assert len(logic) == 1 and logic[0]['components'] == [{'type': 'script', 'lua': 'scripts/game_logic.lua'}], logic
    logic[0]['components'][0]['lua'] = 'scripts/door.blocks'
    json.dump(s, open(scene, 'w', encoding='utf-8', newline='\n'), indent=2)
    dest = os.path.join(project, 'scripts', 'door.blocks')
    if blocks is None:
        shutil.copy(fixture, dest)
    else:
        open(dest, 'w', encoding='utf-8', newline='\n').write(blocks)
    return project


out = ''  # the last run's output


def run(args, blocks=None, png=None):
    """The editor on a fresh project copy, then `args`: the project and the
    named controls as last drawn; `out` gets what it printed."""
    global out
    project = project_copy(blocks)
    cmd = [editor, '--size', '1600x960', '--project', project, '--wait', '5'] + OPEN + args + [
        '--wait', '10', '--screenshot', png or os.path.join(project, 'shot.png')]
    r = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=180)
    out = (r.stdout + r.stderr).strip()
    lines = out.splitlines()
    missing = [l for l in lines if 'no such control drawn' in l]
    check('every control the run names was drawn', not missing, missing)
    controls = {}
    for l in lines:
        if l.startswith('control '):
            name, rest = l[len('control '):].rsplit(' at ', 1)
            a, b = rest.split(' ')
            x0, y0 = map(int, a.split(','))
            x1, y1 = map(int, b.split(','))
            controls[name] = (x0, y0, x1, y1)
    return project, controls


def chip(c):
    return ['--click', '@blocks.category.' + c, '--wait', '5']


def drop(name, rect, x, y):
    """Drag control `name` (drawn at `rect`) so its top left lands on x, y."""
    return ['--drag', '@%s,0,0,%d,%d' % (name, x - rect[0], y - rect[1]), '--wait', '5']


def need(c, *names):
    missing = [n for n in names if n not in c]
    if missing:
        check('drawn: ' + ', '.join(missing), False, sorted(k for k in c if k.startswith('blocks.'))[:60])
        print(f'{fails} failures')
        sys.exit(1)
    return [c[n] for n in names]


def lua(project):
    r = subprocess.run([cli, 'blocks', os.path.join(project, 'scripts', 'door.blocks')], capture_output=True, text=True)
    return r.returncode, r.stdout, r.stderr.strip()


def lua_lines():
    return [l for l in out.splitlines() if l.startswith('blocks: lua')]


SAVE = ['--key', 'ctrl+s', '--wait', '5']

# The hat, then the Screen section.
_, c = run([])
canvas, hatPal = need(c, 'blocks.canvas', 'blocks.palette.on_interact')
origin = (canvas[0] + 24, canvas[1] + 24)
hatScreen = (origin[0] + HAT_AT[0], origin[1] + HAT_AT[1])
steps = drop('blocks.palette.on_interact', hatPal, hatScreen[0], hatScreen[1]) + chip('Screen')
_, c = run(steps)
check('the palette has a Screen section', 'blocks.category.Screen' in c, sorted(k for k in c if k.startswith('blocks.category')))
offered = [op for op in SCREEN_OPS if 'blocks.palette.' + op in c]
check('the Screen section offers every Screen block', offered == SCREEN_OPS, offered)
hat, fadePal = need(c, 'blocks.block.s0.b0', 'blocks.palette.fade_out')
steps += drop('blocks.palette.fade_out', fadePal, hat[0], hat[3])
_, c = run(steps)
check('a new fade block starts black', 'blocks: lua 3 lines, 0 errors' in out, lua_lines()[-3:])

# The colour field opens a picker; a click on the top edge of its square, half way across, is a bright red
# (the hue starts at red from black). The square fills the picker's width but the hue bar.
_, c = run(steps + ['--click', '@blocks.field.s0.b1', '--wait', '5'])
(picker,) = need(c, 'blocks.colourpicker')
x0, y0, x1, y1 = picker
at = (x0 + (x1 - x0) // 2, y0 + 4)
# Escape closes the picker; a palette click gives the block tab the keyboard back for Ctrl+S.
steps += ['--click', '@blocks.field.s0.b1', '--wait', '5', '--click', '%d,%d' % at, '--wait', '5', '--key', 'escape', '--wait', '5'] + \
    chip('Screen')
project, c = run(steps + SAVE)
picked = [l.split()[-1] for l in out.splitlines() if l.startswith('blocks: colour ')]
check('a click in the picker sets the colour', picked, out.splitlines()[-12:])
saved = json.load(open(os.path.join(project, 'scripts', 'door.blocks'), encoding='utf-8'))
blk = saved['stacks'][0]['blocks'][1] if saved['stacks'] and len(saved['stacks'][0]['blocks']) > 1 else {}
field = blk.get('field', '')
m = re.fullmatch(r'#([0-9A-F]{2})([0-9A-F]{2})([0-9A-F]{2})', field)
rgb = tuple(int(g, 16) for g in m.groups()) if m else None
check('the saved field is the picked "#RRGGBB"', blk.get('op') == 'fade_out' and m and picked and field == picked[-1], (blk, picked))
check('... a red: red near full, green and blue equal and lower', rgb and rgb[0] >= 240 and rgb[1] == rgb[2] and rgb[1] < 200, rgb)
rc, lout, err = lua(project)
want = 'function onInteract(self)\n    Screen.FadeOut(%d, %d, %d, 30)\nend\n' % (rgb if rgb else (0, 0, 0))
check('splashpack-cli blocks makes Screen.FadeOut with the same numbers', rc == 0 and lout == want, (rc, lout, err))
check('the Lua panel counts that Lua, with no errors', lua_lines() and lua_lines()[-1] == 'blocks: lua %d lines, 0 errors' % lout.count('\n'),
      lua_lines()[-3:])

# A colour field that is not "#RRGGBB" (a hand-edited file): an error naming the block.
BAD = json.dumps({'format': 'splashedit-blocks', 'version': 1, 'variables': [], 'stacks': [
    {'x': HAT_AT[0], 'y': HAT_AT[1], 'blocks': [{'op': 'on_interact'},
                                                {'op': 'flash', 'field': 'red', 'args': [{'op': 'number', 'field': '10'}]}]}]})
run([], blocks=BAD)
check('a colour that is not #RRGGBB is a Lua panel error', 'blocks: lua error stack 1, block 2 (flash): not a colour: red' in out,
      lua_lines()[-3:])

# The picture: every Screen block.
if shot:
    screen = open(os.path.join(here, 'blocks', 'screen.blocks'), encoding='utf-8').read()
    _, c = run(chip('Screen'), blocks=screen, png=shot)
    check('screen.blocks opens with its blocks', 'blocks.block.s1.b2' in c and 'blocks: lua 59 lines, 0 errors' in out, lua_lines()[-3:])
    check('the picture is saved', os.path.exists(shot), shot)

print(f'{fails} failures' if fails else 'all passed')
sys.exit(1 if fails else 0)
