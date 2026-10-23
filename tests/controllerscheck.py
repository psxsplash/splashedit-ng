# Controllers, driven through the GUI. The block editor: the palette's
# Controllers category draws its six blocks; a button hat's player and a
# "held" block's button and player are picked from their lists (typed into
# the list's search, Enter takes the first), the tab saved: the file is then
# version 2 with the players, and `splashpack-cli blocks` turns it into the
# expected Lua; Ctrl+Z takes the last pick back. A "device of player" block
# dragged from the palette lands with its player and device wells. The Scene
# panel's Controllers section: what Play plugs in per player and the
# multitaps, saved to the play settings (--play-settings) and read back; a
# multitap's slots appear with their tap, and offer a pad or nothing. Controls
# are aimed at by name (blocks.category.Controllers, blocks.palette.<op>,
# blocks.player.<path>, blocks.field.<path>, scene.multitap.<port>,
# scene.playdevice.<player>, and "<dropdown>/<item>").
# usage: python3 controllerscheck.py <splashedit> [<splashpack-cli>] [screenshot prefix]   (needs a display: xvfb-run -a)
import json, os, shutil, subprocess, sys, tempfile

editor = os.path.abspath(sys.argv[1])
cli = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else os.path.join(os.path.dirname(editor), 'cli', 'splashpack-cli')
shots = os.path.abspath(sys.argv[3]) if len(sys.argv) > 3 else None
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
fails = 0
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')
EDIT_SCRIPT = '1410,419'  # Game Logic's "Edit script" button at 1600x960 (as in blockseditorcheck.py)
OPEN = ['--select', 'Game Logic', '--click', EDIT_SCRIPT, '--wait', '10']
CONTROLLERS = ['--click', '@blocks.category.Controllers', '--wait', '5']
OPS = ['button_pressed', 'button_released', 'mouse_x', 'mouse_y', 'stick', 'device_is']

# when CROSS is pressed by anyone: if CROSS held by player 1? then log "held".
START = {'format': 'splashedit-blocks', 'version': 1, 'variables': [], 'stacks': [{'x': 40, 'y': 40, 'blocks': [
    {'op': 'on_button_press', 'field': 'CROSS'},
    {'op': 'if', 'args': [{'op': 'button_held', 'field': 'CROSS'}],
     'bodies': [[{'op': 'log', 'args': [{'op': 'text', 'field': 'held'}]}]]}]}]}
WANT_LUA = ('function onButtonPress(self, button, byPlayer)\n'
            '    if button == Input.CROSS then\n'
            '        if byPlayer == 3 then\n'
            '            if (Input.GetDevice(8) == "mouse" and Input.IsHeld(8, Input.MOUSE_LEFT)) then\n'
            '                Debug.Log("held")\n'
            '            end\n'
            '        end\n'
            '    end\n'
            'end\n')


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def project_copy():
    """The courtyard with Game Logic running scripts/door.blocks (START), and its own play settings."""
    project = os.path.join(tempfile.mkdtemp(), 'courtyard')
    shutil.copytree(example, project)
    scene = os.path.join(project, 'courtyard.scene')
    s = json.load(open(scene, encoding='utf-8'))
    logic = [o for o in s['objects'] if o['name'] == 'Game Logic']
    assert len(logic) == 1, logic
    logic[0]['components'][0]['lua'] = 'scripts/door.blocks'
    json.dump(s, open(scene, 'w', encoding='utf-8', newline='\n'), indent=2)
    json.dump(START, open(os.path.join(project, 'scripts', 'door.blocks'), 'w', encoding='utf-8', newline='\n'), indent=2)
    with open(os.path.join(os.path.dirname(project), 'play.cfg'), 'w') as fh:
        fh.write('players=1\n')
    return project


def run(project, args, shot=None):
    """The editor on `project`, then `args`; the output lines and the named controls as last drawn."""
    final = shot or os.path.join(project, 'shot.png')
    cmd = [editor, '--size', '1600x960', '--project', project, '--play-settings', os.path.join(os.path.dirname(project), 'play.cfg'),
           '--wait', '5'] + args + ['--wait', '10', '--screenshot', final]
    r = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=240)
    lines = (r.stdout + r.stderr).strip().splitlines()
    missing = [l for l in lines if 'no such control drawn' in l]
    check('every control the run names was drawn', not missing, missing)
    controls = {}
    for l in lines:
        if l.startswith('control ') and ' at ' in l:
            name, at = l[len('control '):].rsplit(' at ', 1)
            a, b = at.split(' ')
            controls[name] = tuple(float(v) for v in a.split(',') + b.split(','))
    return lines, controls


def blocks(project):
    with open(os.path.join(project, 'scripts', 'door.blocks'), encoding='utf-8') as fh:
        return json.load(fh)


def pick(handle, text):
    return ['--click', '@' + handle, '--wait', '5', '--text', text, '--wait', '3', '--key', 'enter', '--wait', '5']


def shotPath(name):
    return (shots + '-' + name + '.png') if shots else None


# The palette's Controllers category: its six blocks drawn once it is picked.
p = project_copy()
lines, c = run(p, OPEN + CONTROLLERS, shotPath('palette'))
check('a Controllers category chip is drawn', 'blocks.category.Controllers' in c, sorted(k for k in c if k.startswith('blocks.category.')))
check('picking it shows its six blocks', all('blocks.palette.' + op in c for op in OPS),
      [op for op in OPS if 'blocks.palette.' + op not in c])
check('the hat and the held block show their player wells', 'blocks.player.s0.b0' in c and 'blocks.player.s0.b1.a0' in c,
      sorted(k for k in c if k.startswith('blocks.player.')))
canvas, dev = c.get('blocks.canvas'), c.get('blocks.palette.device_is')

# Players and a mouse button picked from the lists, saved.
PICKS = pick('blocks.player.s0.b0', 'player 3') + pick('blocks.field.s0.b1.a0', 'mouse left') + pick('blocks.player.s0.b1.a0', 'player 8')
p = project_copy()
lines, c = run(p, OPEN + PICKS + ['--key', 'ctrl+s', '--wait', '5'], shotPath('picked'))
b = blocks(p)
st = b['stacks'][0]['blocks']
check('the file is version 2', b['version'] == 2, b['version'])
check('the hat keeps player 3', st[0].get('player') == '3', st[0])
held = st[1]['args'][0]
check('the held block keeps mouse left and player 8', held.get('field') == 'MOUSE_LEFT' and held.get('player') == '8', held)
r = subprocess.run([cli, 'blocks', os.path.join(p, 'scripts', 'door.blocks')], capture_output=True, text=True)
check('splashpack-cli turns it into the expected Lua', r.returncode == 0 and r.stdout == WANT_LUA, (r.returncode, r.stdout, r.stderr))

# Ctrl+Z takes the last pick back: the held block is player 1 again (no "player").
p = project_copy()
lines, c = run(p, OPEN + PICKS + ['--key', 'ctrl+z', '--wait', '5', '--key', 'ctrl+s', '--wait', '5'])
held = blocks(p)['stacks'][0]['blocks'][1]['args'][0]
check('Ctrl+Z puts the held block back to player 1', 'player' not in held and held.get('field') == 'MOUSE_LEFT', held)

# A device block from the palette: on the canvas with its player and device wells.
if canvas and dev:
    # Under the stack (canvas 40,40 to about 40,170), inside the canvas: it is narrow beside the Lua column.
    to = (canvas[0] + 24 + 40, canvas[1] + 24 + 260)
    check('the drop point is on the canvas', canvas[0] < to[0] < canvas[2] and canvas[1] < to[1] < canvas[3], (to, canvas))
    p = project_copy()
    lines, c = run(p, OPEN + CONTROLLERS + ['--drag', '@blocks.palette.device_is,0,0,%d,%d' % (to[0] - dev[0], to[1] - dev[1]), '--wait', '5'] +
                   pick('blocks.field.s1.b0', 'analog') + ['--key', 'ctrl+s', '--wait', '5'], shotPath('dragged'))
    st = blocks(p)['stacks']
    check('the dragged block is a loose stack of its own', len(st) == 2 and st[1]['blocks'][0]['op'] == 'device_is', st[1:])
    check('its device picked from the list', len(st) == 2 and st[1]['blocks'][0].get('field') == 'analog', st[1:])
    check('its player well is drawn', 'blocks.player.s1.b0' in c)

# The Scene panel's Controllers section.
# The live export's result sizes the scene inspector's sections: a control found before it lands can
# move before the click does (CI's faster runner), so every settings run waits for it first.
SETTLE = ['--wait-export', '--wait', '5']
TAP1 = SETTLE + ['--scroll-to', '@scene.multitap.1', '--wait', '3', '--click', '@scene.multitap.1', '--wait', '5']
p = project_copy()
cfg = os.path.join(os.path.dirname(p), 'play.cfg')
lines, c = run(p, SETTLE + ['--scroll-to', '@scene.multitap.1', '--wait', '3'])
scene = [l for l in lines if l.startswith('scene: controllers')]
check('untouched: no tap, players 1 and 2 pads', scene[-1:] == ['scene: controllers tap1 off tap2 off p1 pad p2 pad'], scene)
check('no slot rows without a tap', 'scene.playdevice.2' in c and 'scene.playdevice.3' not in c and 'scene.multitap.2' in c,
      sorted(k for k in c if k.startswith('scene.playdevice')))
lines, c = run(p, TAP1 + ['--scroll-to', '@scene.playdevice.1', '--wait', '3', '--click', '@scene.playdevice.1', '--wait', '5',
                          '--click', '@scene.playdevice.1/Mouse', '--wait', '5',
                          '--scroll-to', '@scene.playdevice.4', '--wait', '3', '--click', '@scene.playdevice.4', '--wait', '5',
                          '--click', '@scene.playdevice.4/None', '--wait', '5', '--scroll-to', '@scene.playdevice.5', '--wait', '3'],
               shotPath('settings'))
scene = [l for l in lines if l.startswith('scene: controllers')]
check('the multitap switch turns the tap on', 'scene: controllers tap1 on tap2 off p1 pad p2 pad p3 pad p4 pad p5 pad' in scene, scene)
check('a tap on port 1, player 1 a mouse, player 4 none',
      scene[-1:] == ['scene: controllers tap1 on tap2 off p1 mouse p2 pad p3 pad p4 none p5 pad'], scene)
check('the tap brings players 3 to 5', all('scene.playdevice.%d' % n in c for n in (3, 4, 5)) and 'scene.playdevice.6' not in c,
      sorted(k for k in c if k.startswith('scene.playdevice')))
text = open(cfg).read()
check('saved to the play settings', 'play_devices=mouse,pad,pad,none,pad,pad,pad,pad\n' in text and 'multitap=1,0\n' in text, text)
# Read back by a new run; a slot's list offers a pad or nothing.
lines, c = run(p, SETTLE + ['--scroll-to', '@scene.playdevice.3', '--wait', '3', '--click', '@scene.playdevice.3', '--wait', '5'],
               shotPath('slotlist'))
scene = [l for l in lines if l.startswith('scene: controllers')]
check('a new run reads them back', scene[-1:] == ['scene: controllers tap1 on tap2 off p1 mouse p2 pad p3 pad p4 none p5 pad'], scene)
check('a slot offers a pad or nothing', 'scene.playdevice.3/Pad' in c and 'scene.playdevice.3/None' in c and
      'scene.playdevice.3/Mouse' not in c and 'scene.playdevice.3/Analog pad' not in c,
      sorted(k for k in c if k.startswith('scene.playdevice.3/')))
lines, c = run(p, SETTLE + ['--scroll-to', '@scene.playdevice.1', '--wait', '3', '--click', '@scene.playdevice.1', '--wait', '5'])
check('a port offers pad, analog pad, mouse and none', all('scene.playdevice.1/' + n in c for n in ('Pad', 'Analog pad', 'Mouse', 'None')),
      sorted(k for k in c if k.startswith('scene.playdevice.1/')))

print('controllerscheck: %d failure(s)' % fails)
sys.exit(1 if fails else 0)
