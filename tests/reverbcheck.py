# The reverb rows, driven through the GUI on the courtyard split into two Rooms (West, East) with a Door portal,
# a trigger box Zone and a looping clip Sound: the scene's preset and CD music switch, a Room's and a trigger
# zone's preset picked from the shared picker (--click @<control>/<item>), and the clip's reverb send. Each saves
# by name only when not the default, reports its scripted line, and the SPU RAM meter adds the largest preset's
# work area above the clips ("statusbar: spu reverb <preset> <bytes> clips <end>"). One Ctrl+Z undoes a pick.
# Controls are reached by name (--scroll-to @name, --click @name), never by pixel positions.
# usage: python3 reverbcheck.py <splashedit> [shot.png]   (needs a display: xvfb-run -a)
import json, os, re, shutil, subprocess, sys, tempfile

editor = os.path.abspath(sys.argv[1])
keep = sys.argv[2] if len(sys.argv) > 2 else None
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
fails = 0
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')
WORK = {'off': 0, 'hall': 0xADE0, 'spaceEcho': 0xF6C0, 'delay': 0x18040, 'studioSmall': 0x1F40}


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def obj(name, pos, components, rot=(0, 0, 0, 1)):
    return {'name': name, 'active': True, 'transform': {'position': list(pos), 'rotation': list(rot), 'scale': [1, 1, 1]},
            'components': components, 'children': []}


def project_copy():
    project = os.path.join(tempfile.mkdtemp(), 'courtyard')
    shutil.copytree(example, project)
    os.makedirs(os.path.join(project, 'audio'))
    shutil.copy(os.path.join(here, 'audio', 'noise22.wav'), os.path.join(project, 'audio', 'noise.wav'))
    scene = os.path.join(project, 'courtyard.scene')
    s = json.load(open(scene, encoding='utf-8'))
    room = lambda: {'type': 'room', 'size': [7, 4.4, 12], 'offset': [0, 0, 0]}
    s['objects'] += [obj('West', (-3.6, 2, 0.5), [room()]), obj('East', (3.6, 2, 0.5), [room()]),
                     obj('Door', (0, 1.5, -1.5), [{'type': 'portal', 'roomA': 'West', 'roomB': 'East', 'width': 2, 'height': 3}],
                         (0, 0.70710678, 0, 0.70710678)),
                     obj('Zone', (2, 1, 3), [{'type': 'trigger', 'size': [2, 2, 2], 'lua': None}]),
                     obj('Sound', (0, 0, 0), [{'type': 'audio', 'clip': 'audio/noise.wav', 'clipName': 'noise',
                                               'sampleRate': 22050, 'loop': True, 'defaultVolume': 100,
                                               'trimLeadingSilence': False}])]
    json.dump(s, open(scene, 'w', encoding='utf-8'), indent=2)
    return project, scene


mid = None
last_shot = None


def run(args, select=None):
    """Runs the editor on a fresh copy (`select` selected, else the scene inspector), then `args` and Ctrl+S;
    returns the saved scene and the output lines without the list of controls."""
    global mid, last_shot
    project, scene = project_copy()
    snap = os.path.join(os.path.dirname(project), 'mid.scene')
    args = [a.replace('{scene}', scene).replace('{snap}', snap) for a in args]
    last_shot = os.path.join(project, 'shot.png')
    cmd = [editor, '--size', '1600x960', '--project', project] + (['--select', select] if select else []) + \
          ['--wait', '5'] + args + ['--key', 'ctrl+s', '--wait', '20', '--screenshot', last_shot]
    r = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=180)
    mid = json.load(open(snap, encoding='utf-8')) if os.path.exists(snap) else None
    lines = (r.stdout + r.stderr).strip().splitlines()
    missing = [l for l in lines if 'no such control drawn' in l or 'scrolls no further' in l]
    check('every control the script names was drawn', not missing, missing)
    return json.load(open(scene, encoding='utf-8')), [l for l in lines if not l.startswith('control ')]


def W(n):
    return ['--wait', str(n)]


def pick(control, item):
    """Opens the picker named `control` (scrolled into view first) and clicks its `item`."""
    return ['--scroll-to', '@' + control] + W(3) + ['--click', '@' + control] + W(3) + ['--click', '@' + control + '/' + item] + W(3)


def comp(s, name, kind):
    for o in s['objects']:
        if o['name'] == name:
            return next((c for c in o['components'] if c['type'] == kind), None)


def last(lines, pattern):
    hits = [l for l in lines if re.match(pattern, l)]
    return hits[-1] if hits else None


def spu(lines):
    """The meter's reverb line, once the export landed: (preset, work area bytes, clips end), and the meter's used."""
    l = last(lines, r'^statusbar: spu reverb ')
    u = last(lines, r'^statusbar: spu \d+/')
    if not l or not u:
        return None
    p = l.split()
    return p[3], int(p[4]), int(p[6]), int(u.split()[2].split('/')[0])


UNDO = ['--key', 'ctrl+z', '--wait', '3']
SAVE_MID = ['--key', 'ctrl+s', '--wait', '10', '--copy-file', '{scene}@{snap}', '--wait', '2']

# 1. Nothing set: no keys, the scene reports off, no work area on the meter.
s, out = run(W(15))
check('save alone writes no reverb keys', 'reverb' not in json.dumps(s) and 'cdReverb' not in s['settings'], s['settings'])
check('the scene reports reverb off, CD off', last(out, r'^reverb: scene ') == 'reverb: scene off cd off', out[-20:])
m = spu(out)
check('no reverb: no work area, the meter is the clips alone', m and m[0] == 'off' and m[1] == 0 and m[3] == m[2], m)
clips = m[2] if m else 0

# 2. The scene's picker and the CD switch.
s, out = run(pick('scene.reverb', 'Hall') + ['--scroll-to', '@scene.cdreverb'] + W(3) + ['--click', '@scene.cdreverb'] + W(15))
check('picking Hall saves "reverb": "hall"', s['settings'].get('reverb') == 'hall', s['settings'])
check('the CD switch saves "cdReverb": true', s['settings'].get('cdReverb') is True, s['settings'])
check('the scene reports hall, CD on', last(out, r'^reverb: scene ') == 'reverb: scene hall cd on', out[-20:])
m = spu(out)
check('the meter adds the Hall work area above the clips', m and m[0] == 'hall' and m[1] == WORK['hall'] and m[2] == clips and
      m[3] == clips + WORK['hall'], m)

# 3. A Room's picker, with the Room selected.
s, out = run(pick('obj.rreverb', 'Space echo') + W(15), select='East')
check('the Room is selected', 'selected: East' in out, [l for l in out if l.startswith('selected')])
check('picking Space echo saves the room\'s "reverb": "spaceEcho"', (comp(s, 'East', 'room') or {}).get('reverb') == 'spaceEcho',
      comp(s, 'East', 'room'))
check('West keeps no reverb key', 'reverb' not in (comp(s, 'West', 'room') or {}), comp(s, 'West', 'room'))
check('the room reports it', last(out, r'^reverb: room East ') == 'reverb: room East spaceEcho', out[-20:])
m = spu(out)
check('the meter takes the Space echo work area', m and m[0] == 'spaceEcho' and m[3] == clips + WORK['spaceEcho'], m)
shot_room = last_shot

# 4. A trigger zone's picker; the largest preset (Delay) sets the reservation.
s, out = run(pick('obj.treverb', 'Delay') + W(15), select='Zone')
check('picking Delay saves the trigger\'s "reverb": "delay"', (comp(s, 'Zone', 'trigger') or {}).get('reverb') == 'delay',
      comp(s, 'Zone', 'trigger'))
check('the trigger reports it', last(out, r'^reverb: trigger Zone ') == 'reverb: trigger Zone delay', out[-20:])
m = spu(out)
check('the meter takes the Delay work area', m and m[0] == 'delay' and m[3] == clips + WORK['delay'], m)

# 5. The clip's send switch: off saves "reverbSend": false; back on drops the key.
s, out = run(['--scroll-to', '@obj.areverb'] + W(3) + ['--click', '@obj.areverb'] + W(5), select='Sound')
check('the send switch saves "reverbSend": false', (comp(s, 'Sound', 'audio') or {}).get('reverbSend') is False,
      comp(s, 'Sound', 'audio'))
check('the clip reports its send off', last(out, r'^reverb: send Sound ') == 'reverb: send Sound off', out[-20:])
s, out = run(['--scroll-to', '@obj.areverb'] + W(3) + ['--click', '@obj.areverb'] + W(3) + ['--click', '@obj.areverb'] + W(5),
             select='Sound')
check('a second click turns it back on and the key is gone', 'reverbSend' not in (comp(s, 'Sound', 'audio') or {}),
      comp(s, 'Sound', 'audio'))

# 6. One Ctrl+Z undoes a pick.
s, out = run(pick('obj.rreverb', 'Hall') + SAVE_MID + UNDO, select='East')
check('one Ctrl+Z takes the pick back', mid is not None and (comp(mid, 'East', 'room') or {}).get('reverb') == 'hall' and
      'reverb' not in (comp(s, 'East', 'room') or {}), (mid and comp(mid, 'East', 'room'), comp(s, 'East', 'room')))

# The screenshot: the Room's inspector after the Space echo pick.
if keep and shot_room and os.path.exists(shot_room):
    shutil.copy(shot_room, keep)
    print('screenshot', keep)
print(f'{fails} failures')
sys.exit(1 if fails else 0)
