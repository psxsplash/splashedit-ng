# Export light/light.scene plus variations and read the point light table, the
# dynamic-lighting object flags and the light tracks back the way psxsplash's
# splashpack.cpp does, and check that runtime lights are left out of a
# runtime-lit mesh's bake.
# usage: python3 lightcheck.py <splashpack-cli>
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


base = json.load(open(os.path.join(here, 'light', 'light.scene')))


def export(edit=lambda s: None):
    s = json.loads(json.dumps(base))
    for o in s['objects']:
        for c in o['components']:
            if c['type'] == 'mesh':
                c['mesh'] = os.path.join(here, 'light', c['mesh'])
                for m in c['materials']:
                    m['texture'] = os.path.join(here, 'light', m['texture'])
    edit(s)
    path = os.path.join(tmp, 'l.scene')
    json.dump(s, open(path, 'w'))
    out = os.path.join(tmp, 'l.splashpack')
    if os.path.exists(out):
        os.remove(out)
    r = subprocess.run([cli, 'export', path, '-o', out], capture_output=True, text=True)
    return r, (open(out, 'rb').read() if r.returncode == 0 else b'')


def obj(s, name):
    return next(o for o in s['objects'] if o['name'] == name)


def comp(s, name):
    return obj(s, name)['components'][0]


def read(d):
    version, = struct.unpack_from('<H', d, 2)
    nlua, ngo = struct.unpack_from('<HH', d, 4)
    header = 156 if version >= 25 else 148 if version >= 24 else 144
    objs = []
    for i in range(ngo):
        o = header + nlua * 8 + i * 92
        mesh, = struct.unpack_from('<I', d, o)
        poly, = struct.unpack_from('<H', d, o + 52)
        flags, = struct.unpack_from('<I', d, o + 56)
        colours = [d[mesh + t * 52 + 24:mesh + t * 52 + 36] for t in range(poly)]
        objs.append(dict(flags=flags, colours=colours))
    names = d[struct.unpack_from('<I', d, 60)[0]:]
    objnames = []
    p = 0
    for i in range(ngo):
        n = names[p]
        objnames.append(names[p + 1:p + 1 + n].decode())
        p += n + 2
    lights = []
    table = struct.unpack_from('<I', d, 144)[0] if version >= 24 else 0
    if table:
        count, = struct.unpack_from('<H', d, table)
        for i in range(count):
            x, y, z, rad, inten, r, g, b, fl, _, noff = struct.unpack_from('<iiiiHBBBBHI', d, table + 4 + i * 28)
            lights.append(dict(pos=(x, y, z), radius=rad, intensity=inten, rgb=(r, g, b), flags=fl,
                               name=cstr(d, noff)))
    return version, table, dict(zip(objnames, objs)), lights


r, d = export()
check('export', r.returncode == 0, r.stderr)
version, table, objs, lights = read(d)
check('version 26', version == 26, version)
check('light table offset set and aligned', table != 0 and table % 4 == 0, table)
check('disabled runtime light still exported', [l['name'] for l in lights] == ['Red', 'Spare'], lights)
red = lights[0]
check('position fp12 / gte, y negated', red['pos'] == (0, round(-0.5 / 100 * 4096), round(2.8 / 100 * 4096)), red)
check('radius fp12 / gte', red['radius'] == round(3.0 / 100 * 4096), red)
check('intensity 4.12', red['intensity'] == 8192, red)
check('colour bytes', red['rgb'] == (255, 51, 26), red)
check('enabled flag', red['flags'] == 1 and lights[1]['flags'] == 0, lights)
check('auto + reached -> dynamicLit', objs['Lit']['flags'] & 0x30000 == 0x10000, hex(objs['Lit']['flags']))
check('auto + out of range -> not lit', objs['Far']['flags'] & 0x30000 == 0, hex(objs['Far']['flags']))
check('smooth -> both bits', objs['Smooth']['flags'] & 0x30000 == 0x30000, hex(objs['Smooth']['flags']))
check('off -> not lit', objs['Unlit']['flags'] & 0x30000 == 0, hex(objs['Unlit']['flags']))
check('directional is not a runtime light', all(l['name'] != 'Sun' for l in lights), lights)

# Bake: a runtime-lit mesh leaves the runtime light out of its vertex colours,
# a mesh that is not runtime-lit bakes it.
_, d_baked = export(lambda s: comp(s, 'Red').update(runtime=False))
_, d_none = export(lambda s: s['objects'].remove(obj(s, 'Red')))
objs_baked = read(d_baked)[2]
objs_none = read(d_none)[2]
check('lit mesh: runtime light not baked', objs['Lit']['colours'] == objs_none['Lit']['colours'])
check('lit mesh: baked light changes colours (control)', objs_baked['Lit']['colours'] != objs_none['Lit']['colours'])
check('off mesh: runtime light still baked', objs['Unlit']['colours'] == objs_baked['Unlit']['colours'])

# No runtime lights -> no light table.
_, d23 = export(lambda s: [comp(s, n).update(runtime=False) for n in ('Red', 'Spare')])
v, t, o23, l23 = read(d23)
check('baked-only scene has no light table', t == 0 and not l23, (v, t))
check('no dynamic flags without runtime lights', all(x['flags'] & 0x30000 == 0 for x in o23.values()))

# The table holds 16; extra lights are dropped with a warning.
def many(s):
    for i in range(18):
        s['objects'].append({'name': 'P%02d' % i, 'active': True,
                             'transform': {'position': [0, 20, i], 'rotation': [0, 0, 0, 1], 'scale': [1, 1, 1]},
                             'components': [{'type': 'light', 'kind': 'point', 'runtime': True, 'range': 1}],
                             'children': []})
r, d = export(many)
check('cap at 16', len(read(d)[3]) == 16 and 'PS1 holds 16' in r.stderr + r.stdout, (len(read(d)[3]), r.stderr))

# Only point lights run on the console.
r, _ = export(lambda s: comp(s, 'Sun').update(runtime=True))
check('runtime directional refused', r.returncode != 0 and 'point' in r.stderr, r.stderr)
r, _ = export(lambda s: comp(s, 'Lit').update(dynamicLighting='sometimes'))
check('unknown dynamicLighting refused', r.returncode != 0, r.stderr)

# Light tracks (types 14-18): no target name, the track's fourth byte is the
# light's index in the runtime table, keys in the light table's own units.
def tracks(d, countOff, tableOff, animation):
    n, = struct.unpack_from('<H', d, countOff)
    table, = struct.unpack_from('<I', d, tableOff)
    out = []
    for i in range(n):
        doff, = struct.unpack_from('<I', d, table + i * 12)
        ntr, = struct.unpack_from('<B', d, doff + 2)
        troff, = struct.unpack_from('<I', d, doff + 4)
        for t in range(ntr):
            typ, nk, tnl, li, tnoff, kfoff = struct.unpack_from('<BBBBII', d, troff + t * 12)
            keys = [struct.unpack_from('<Hhhh', d, kfoff + k * 8) for k in range(nk)]
            out.append(dict(type=typ, index=li, namelen=tnl, nameoff=tnoff, keys=keys))
    return out


def key(f, v):
    return {'frame': f, 'value': v}


def lightScene(s, target='Spare', intensity=1.5):
    s['cutscenes'] = [{'name': 'Glow', 'durationFrames': 60, 'audioEvents': [], 'tracks': [
        {'type': 'lightPosition', 'target': target, 'keyframes': [key(30, [1, 2, -3]), key(0, [0, 0.5, 0])]},
        {'type': 'lightColor', 'target': target, 'keyframes': [key(0, [1, 0.2, 0])]},
        {'type': 'lightIntensity', 'target': target, 'keyframes': [key(0, [intensity, 0, 0])]},
        {'type': 'lightRadius', 'target': target, 'keyframes': [key(0, [4, 0, 0]), key(10, [-1, 0, 0])]},
        {'type': 'lightEnabled', 'target': target, 'keyframes': [key(0, [1, 0, 0]), key(20, [0.2, 0, 0])]}]}]
    s['animations'] = [{'name': 'Pulse', 'durationFrames': 30, 'tracks': [
        {'type': 'lightIntensity', 'target': 'Red', 'keyframes': [key(0, [0, 0, 0]), key(15, [2, 0, 0])]}]}]


r, d = export(lightScene)
check('light tracks export', r.returncode == 0, r.stderr)
cs = tracks(d, 84, 88, False)
check('light track types 14-18', [t['type'] for t in cs] == [14, 15, 16, 17, 18], cs)
check('light index byte = table index of Spare', all(t['index'] == 1 for t in cs), cs)
check('light tracks carry no name', all(t['namelen'] == 0 and t['nameoff'] == 0 for t in cs), cs)
check('position keys sorted, fp12 / gte, y negated',
      [k[1:] for k in cs[0]['keys']] == [(0, round(-0.5 / 100 * 4096), 0),
                                        (round(1 / 100 * 4096), round(-2 / 100 * 4096), round(-3 / 100 * 4096))] and
      [k[0] & 0x1FFF for k in cs[0]['keys']] == [0, 30], cs[0])
check('colour keys 0..255', cs[1]['keys'][0][1:] == (255, 51, 0), cs[1])
check('intensity key 4.12', cs[2]['keys'][0][1:] == (6144, 0, 0), cs[2])
check('radius key fp12 / gte, negative -> 0', [k[1] for k in cs[3]['keys']] == [round(4 / 100 * 4096), 0], cs[3])
check('enabled keys 0/1', [k[1] for k in cs[4]['keys']] == [1, 0], cs[4])
an = tracks(d, 104, 108, True)
check('animation light track indexes Red', [(t['type'], t['index']) for t in an] == [(16, 0)], an)
check('animation intensity keys', [k[1] for k in an[0]['keys']] == [0, 8192], an)

r, _ = export(lambda s: lightScene(s, 'Nope'))
check('unknown light target refused', r.returncode != 0 and "no runtime point light named 'Nope'" in r.stderr, r.stderr)
r, _ = export(lambda s: lightScene(s, 'Sun'))
check('baked light target refused', r.returncode != 0 and "'Sun'" in r.stderr, r.stderr)
r, _ = export(lambda s: (lightScene(s), comp(s, 'Spare').update(runtime=False)))
check('light switched to baked refused', r.returncode != 0 and "'Spare'" in r.stderr, r.stderr)
r, d = export(lambda s: lightScene(s, intensity=9))
check('intensity over 8 clamps with a warning',
      r.returncode == 0 and tracks(d, 84, 88, False)[2]['keys'][0][1] == 32767 and 'clamped to 8' in r.stderr + r.stdout,
      r.stderr)

print('%d/%d checks passed' % (checks - fails, checks))
sys.exit(1 if fails else 0)
