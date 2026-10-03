# Export light/light.scene plus variations and read the point light table and
# the dynamic-lighting object flags back the way psxsplash's splashpack.cpp
# does, and check that runtime lights are left out of a runtime-lit mesh's bake.
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
    header = 148 if version >= 24 else 144
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
check('version 24 with runtime lights', version == 24, version)
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

# No runtime lights -> v23 with the 144-byte header, as before.
_, d23 = export(lambda s: [comp(s, n).update(runtime=False) for n in ('Red', 'Spare')])
v, t, o23, l23 = read(d23)
check('baked-only scene stays v23', v == 23 and t == 0 and not l23, (v, t))
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

print('%d/%d checks passed' % (checks - fails, checks))
sys.exit(1 if fails else 0)
