# Export cutscene/cutscene.scene plus variations and read the cutscene table
# back the way psxsplash's splashpack.cpp does: names, track targets, keyframe
# encodings per track type, key order, audio events, and the errors that
# replace tracks the engine would silently skip.
# usage: python3 cutscenecheck.py <splashpack-cli>
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


base = json.load(open(os.path.join(here, 'cutscene', 'cutscene.scene')))


def absolutise(s):
    for o in s['objects']:
        for c in o['components']:
            if c['type'] == 'mesh':
                c['mesh'] = os.path.join(here, 'cutscene', c['mesh'])
                for m in c['materials']:
                    m['texture'] = os.path.join(here, 'cutscene', m['texture'])
    s['settings']['script'] = os.path.join(here, 'cutscene', s['settings']['script'])


def export(edit):
    s = json.loads(json.dumps(base))
    absolutise(s)
    edit(s)
    path = os.path.join(tmp, 'c.scene')
    json.dump(s, open(path, 'w'))
    out = os.path.join(tmp, 'c.splashpack')
    r = subprocess.run([cli, 'export', path, '-o', out], capture_output=True, text=True)
    return r, (open(out, 'rb').read() if r.returncode == 0 else b'')


def read(d):
    n = struct.unpack_from('<H', d, 84)[0]
    table = struct.unpack_from('<I', d, 88)[0]
    out = {}
    for i in range(n):
        doff, nlen, _, _, _, noff = struct.unpack_from('<IBBBBI', d, table + i * 12)
        dur, ntr, nau, troff, auoff = struct.unpack_from('<HBBII', d, doff)
        tracks = []
        for t in range(ntr):
            typ, nk, tnl, _, tnoff, kfoff = struct.unpack_from('<BBBBII', d, troff + t * 12)
            keys = [struct.unpack_from('<Hhhh', d, kfoff + k * 8) for k in range(nk)]
            tracks.append(dict(type=typ, target=cstr(d, tnoff),
                               keys=[(k[0] & 0x1FFF, k[0] >> 13, k[1:]) for k in keys]))
        audio = [struct.unpack_from('<HBBB', d, auoff + a * 8) for a in range(nau)]
        out[cstr(d, noff)] = dict(duration=dur, tracks=tracks, audio=audio)
    return out


# cutsceneCount is at 84 and the table offset at 88; without cutscenes both are 0.
r, d = export(lambda s: s.pop('cutscenes'))
check('no cutscenes: count 0', r.returncode == 0 and struct.unpack_from('<H', d, 84)[0] == 0 == struct.unpack_from('<I', d, 88)[0], r.stderr)


def rich(s):
    s['objects'][0]['components'].append({'type': 'audio', 'clip': None, 'clipName': 'boom'})
    s['canvases'][0]['elements'].append({'type': 'box', 'name': 'r', 'rect': [-10, 0, 10, 10],
                                         'anchorMin': [1, 0], 'anchorMax': [1, 0]})
    s['cutscenes'][0]['tracks'] += [
        {'type': 'objectRotation', 'target': 'Crate', 'keyframes': [{'frame': 30, 'value': [90, 45, -180], 'interp': 'easeIn'}]},
        {'type': 'uiPosition', 'target': 'hud/r', 'keyframes': [{'frame': 0, 'value': [-10, 5, 0], 'interp': 'step'}]},
        {'type': 'uiColor', 'target': 'hud/t', 'keyframes': [{'frame': 0, 'value': [1, 0.5, 0]}]},
        {'type': 'cameraH', 'keyframes': [{'frame': 9, 'value': [5000, 0, 0]}, {'frame': 3, 'value': [0, 0, 0]}]},
        {'type': 'objectActive', 'target': 'Crate', 'keyframes': [{'frame': 60, 'value': [0, 0, 0]}]},
    ]
    s['cutscenes'][0]['audioEvents'] = [{'frame': 40, 'clip': 'boom', 'volume': 128, 'pan': 0},
                                        {'frame': 10, 'clip': 'boom'}]


r, d = export(rich)
check('export', r.returncode == 0, r.stderr)
cs = read(d) if d else {}
c = cs.get('slide', dict(tracks=[], audio=[], duration=0))
t = {tr['type']: tr for tr in c['tracks']}
check('one cutscene, duration', list(cs) == ['slide'] and c['duration'] == 120, (list(cs), c['duration']))
check('object position: fp12 of x/gte, y negated', t[2]['target'] == 'Crate' and
      t[2]['keys'] == [(0, 0, (-61, 0, 164)), (120, 0, (61, 0, 164))], t.get(2))
check('object rotation: 1024 per 180 degrees, x and z negated, easeIn', t[3]['keys'] == [(30, 2, (-512, 256, 1024))], t.get(3))
check('ui position takes the anchor correction', t[8]['target'] == 'hud/r' and t[8]['keys'] == [(0, 1, (-8, 5, 0))], t.get(8))
check('ui colour 0..1 to 0..255', t[9]['keys'] == [(0, 0, (255, 128, 0))], t.get(9))
check('camera H clamped and keys sorted', t[10]['target'] == '' and
      [k[0] for k in t[10]['keys']] == [3, 9] and t[10]['keys'][0][2][0] == 1 and t[10]['keys'][1][2][0] == 1024, t.get(10))
check('ui progress', t[7]['keys'] == [(0, 0, (0, 0, 0)), (120, 0, (100, 0, 0))], t.get(7))
check('object active', t[4]['keys'] == [(60, 0, (0, 0, 0))], t.get(4))
check('audio events sorted, clip index, defaults', c['audio'] == [(10, 0, 100, 64), (40, 0, 128, 0)], c['audio'])

for name, edit in [
    ('unknown object', lambda s: s['cutscenes'][0]['tracks'][0].update(target='Nope')),
    ('unknown ui element', lambda s: s['cutscenes'][0]['tracks'][1].update(target='hud/nope')),
    ('ui element without canvas', lambda s: s['cutscenes'][0]['tracks'][1].update(target='t')),
    ('unknown audio clip', lambda s: s['cutscenes'][0].update(audioEvents=[{'frame': 0, 'clip': 'x'}])),
    ('nine tracks', lambda s: s['cutscenes'][0].update(tracks=s['cutscenes'][0]['tracks'] * 5)),
    ('duplicate name', lambda s: s['cutscenes'].append(s['cutscenes'][0])),
    ('frame past 8191', lambda s: s['cutscenes'][0]['tracks'][0]['keyframes'].append({'frame': 8192})),
]:
    r, _ = export(edit)
    check('rejects ' + name, r.returncode != 0, r.stdout + r.stderr)

r, _ = export(lambda s: s['cutscenes'][0]['tracks'][0]['keyframes'].append({'frame': 5, 'value': [900, 0, 0]}))
check('warns on a clamped position', r.returncode == 0 and 'clamped' in r.stdout + r.stderr, r.stdout + r.stderr)

print('%d checks, %d failed' % (checks, fails))
sys.exit(1 if fails else 0)
