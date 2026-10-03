# Export anim/anim.scene plus variations and read the animation table and the
# skin events of cutscenes and animations back the way psxsplash's
# splashpack.cpp does. The skinned arm is the second object and the first
# skinned mesh, so a skin event that wrote the object index would point at a
# mesh that does not exist.
# usage: python3 animcheck.py <splashpack-cli>
import json, os, struct, subprocess, sys, tempfile

cli = sys.argv[1]
here = os.path.dirname(os.path.abspath(__file__))
tmp = tempfile.mkdtemp()
fails = checks = 0


def check(name, ok, detail=''):
    global fails, checks
    checks += 1
    fails += not ok
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))


def cstr(d, off):
    return d[off:d.index(b'\0', off)].decode() if off else ''


base = json.load(open(os.path.join(here, 'anim', 'anim.scene')))


def absolutise(s):
    d = os.path.join(here, 'anim')
    for o in s['objects']:
        for c in o['components']:
            if c['type'] == 'mesh':
                c['mesh'] = os.path.join(d, c['mesh'])
                for m in c['materials']:
                    if m['texture']:
                        m['texture'] = os.path.join(d, m['texture'])
            if c['type'] == 'skin':
                c['clips'] = [os.path.join(d, p) for p in c['clips']]
    s['settings']['script'] = os.path.join(d, s['settings']['script'])


def export(edit=lambda s: None):
    s = json.loads(json.dumps(base))
    absolutise(s)
    edit(s)
    path = os.path.join(tmp, 'a.scene')
    json.dump(s, open(path, 'w'))
    out = os.path.join(tmp, 'a.splashpack')
    r = subprocess.run([cli, 'export', path, '-o', out], capture_output=True, text=True)
    return r, (open(out, 'rb').read() if r.returncode == 0 else b'')


def tracks(d, troff, n):
    out = []
    for t in range(n):
        typ, nk, _, _, tnoff, kfoff = struct.unpack_from('<BBBBII', d, troff + t * 12)
        keys = [struct.unpack_from('<Hhhh', d, kfoff + k * 8) for k in range(nk)]
        out.append(dict(type=typ, target=cstr(d, tnoff), keys=[(k[0] & 0x1FFF, k[1:]) for k in keys]))
    return out


def skin_events(d, off, n):
    return [struct.unpack_from('<HBBB', d, off + i * 8) for i in range(n)]


def read_anims(d):
    n, = struct.unpack_from('<H', d, 104)
    table, = struct.unpack_from('<I', d, 108)
    out = {}
    for i in range(n):
        doff, nlen, _, _, _, noff = struct.unpack_from('<IBBBBI', d, table + i * 12)
        dur, ntr, pad, troff, nsk, _, _, _, skoff = struct.unpack_from('<HBBIBBBBI', d, doff)
        out[cstr(d, noff)] = dict(duration=dur, nlen=nlen, pad=pad, tracks=tracks(d, troff, ntr),
                                  skin=skin_events(d, skoff, nsk), skoff=skoff)
    return out


def read_cutscenes(d):
    n, = struct.unpack_from('<H', d, 84)
    table, = struct.unpack_from('<I', d, 88)
    out = {}
    for i in range(n):
        doff, _, _, _, _, noff = struct.unpack_from('<IBBBBI', d, table + i * 12)
        dur, ntr, nau, troff, auoff, nsk, _, _, _, skoff = struct.unpack_from('<HBBIIBBBBI', d, doff)
        out[cstr(d, noff)] = dict(duration=dur, tracks=tracks(d, troff, ntr), audio=nau,
                                  skin=skin_events(d, skoff, nsk), skoff=skoff)
    return out


# animationCount is at 104 and the table offset at 108; without animations both are 0.
r, d = export(lambda s: s.pop('animations'))
check('no animations: count and offset 0', r.returncode == 0 and struct.unpack_from('<H', d, 104)[0] == 0 ==
      struct.unpack_from('<I', d, 108)[0], r.stderr)

r, d = export()
check('base export succeeds', r.returncode == 0, r.stderr)
anims = read_anims(d)
check('one animation named lift', list(anims) == ['lift'], list(anims))
lift = anims['lift']
check('duration, name length, pad', (lift['duration'], lift['nlen'], lift['pad']) == (60, 4, 0),
      (lift['duration'], lift['nlen'], lift['pad']))
t = lift['tracks']
# Same encoding as a cutscene position key: x, -y, z in 4.12 PSX units (GTE 100).
check('object position track on Crate', len(t) == 1 and t[0]['type'] == 2 and t[0]['target'] == 'Crate', t)
check('position keys encoded like a cutscene', t[0]['keys'] == [(0, (-61, 0, 205)), (60, (-61, -61, 205))], t[0]['keys'])
check('skin event: skin table index 0, not object index 1; clip raise = 1; one-shot',
      lift['skin'] == [(0, 0, 1, 0)], lift['skin'])
check('skin events are 4-byte aligned', lift['skoff'] % 4 == 0, lift['skoff'])

cs = read_cutscenes(d)
wave = cs.get('wave', {})
check('cutscene skin event: bend = clip 0, looping', wave.get('skin') == [(0, 0, 0, 1)], wave.get('skin'))
check('cutscene without audio events or tracks', (wave.get('audio'), wave.get('tracks')) == (0, []), wave)


def add_events(evs):
    return lambda s: s['animations'][0].__setitem__('skinEvents', evs)


r, d = export(add_events([{'frame': 30, 'object': 'Arm', 'clip': 'bend', 'loop': True},
                          {'frame': 5, 'object': 'Arm', 'clip': 'raise'},
                          {'frame': 30, 'object': 'Arm', 'clip': 'raise'}]))
check('skin events sorted by frame, ties keep file order', read_anims(d)['lift']['skin'] ==
      [(5, 0, 1, 0), (30, 0, 0, 1), (30, 0, 1, 0)], r.stderr or read_anims(d)['lift']['skin'])


def second_arm(s):
    arm = json.loads(json.dumps(s['objects'][1]))
    arm['name'] = 'Arm2'
    arm['components'][1]['clips'] = list(reversed(arm['components'][1]['clips']))
    s['objects'].append(arm)
    s['animations'][0]['skinEvents'] = [{'frame': 0, 'object': 'Arm2', 'clip': 'raise'}]


r, d = export(second_arm)
check('second skinned mesh: index 1, clip index in its own clip order', r.returncode == 0 and
      read_anims(d)['lift']['skin'] == [(0, 1, 0, 0)], r.stderr or read_anims(d)['lift']['skin'])


# ---- errors instead of output the engine would skip or mishandle
def err(name, edit, needle):
    r, _ = export(edit)
    check(name, r.returncode != 0 and needle in r.stderr, r.stderr.strip()[:200])


err('skin event on an unknown object', add_events([{'frame': 0, 'object': 'Nope', 'clip': 'raise'}]),
    "no skinned object named 'Nope'")
err('skin event on an object without a skin', add_events([{'frame': 0, 'object': 'Crate', 'clip': 'raise'}]),
    "no skinned object named 'Crate'")
err('skin event with an unknown clip', add_events([{'frame': 0, 'object': 'Arm', 'clip': 'wave'}]),
    "'Arm' has no clip named 'wave'")
err('cutscene skin event with an unknown clip',
    lambda s: s['cutscenes'][0].__setitem__('skinEvents', [{'frame': 0, 'object': 'Arm', 'clip': 'x'}]),
    "cutscene 'wave': 'Arm' has no clip named 'x'")
err('17 skin events', add_events([{'frame': i, 'object': 'Arm', 'clip': 'raise'} for i in range(17)]),
    'more than 16 skin events')
err('camera track in an animation', lambda s: s['animations'][0]['tracks'].append(
    {'type': 'cameraPosition', 'target': '', 'keyframes': [{'frame': 0, 'value': [0, 0, 0]}]}),
    'camera tracks only play in cutscenes')
err('two animations with one name', lambda s: s['animations'].append(json.loads(json.dumps(s['animations'][0]))),
    "two animations are named 'lift'")
err('17 animations', lambda s: s.__setitem__('animations', [dict(s['animations'][0], name='a%d' % i) for i in range(17)]),
    '17 animations')
err('animation name over 24 characters', lambda s: s['animations'][0].__setitem__('name', 'a' * 25), '1..24')
err('animation track on an unknown object', lambda s: s['animations'][0]['tracks'][0].__setitem__('target', 'Nope'),
    "no exported object named 'Nope'")

print(f'{checks - fails} of {checks} checks passed')
sys.exit(1 if fails else 0)
