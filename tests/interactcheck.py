# Export light/light.scene with interactables on two of its meshes and read
# the interactable records back the way psxsplash's splashpack.cpp does: 32
# bytes each from v27, the last 4 being i16 facingCosine = round(cos(angle) *
# 4096) and a reserved u16.
# usage: python3 interactcheck.py <splashpack-cli>
import json, math, os, struct, subprocess, sys, tempfile

cli = sys.argv[1]
here = os.path.dirname(os.path.abspath(__file__))
tmp = tempfile.mkdtemp()
fails = checks = 0


def check(name, ok, detail=''):
    global fails, checks
    checks += 1
    fails += not ok
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))


base = json.load(open(os.path.join(here, 'light', 'light.scene')))


def scene(lit=None, far=None):
    s = json.loads(json.dumps(base))
    for o in s['objects']:
        for c in o['components']:
            if c['type'] == 'mesh':
                c['mesh'] = os.path.join(here, 'light', c['mesh'])
                for m in c['materials']:
                    m['texture'] = os.path.join(here, 'light', m['texture'])
    for name, extra in (('Lit', lit), ('Far', far)):
        if extra is None:
            continue
        o = next(o for o in s['objects'] if o['name'] == name)
        o['components'].append(dict({'type': 'interactable', 'button': 5 if name == 'Lit' else 6,
                                     'promptCanvas': name + 'Prompt'}, **extra))
    return s


def export(s):
    path = os.path.join(tmp, 'i.scene')
    json.dump(s, open(path, 'w'))
    out = os.path.join(tmp, 'i.splashpack')
    if os.path.exists(out):
        os.remove(out)
    r = subprocess.run([cli, 'export', path, '-o', out], capture_output=True, text=True)
    return r, (open(out, 'rb').read() if r.returncode == 0 else b'')


def records(d, size=32):
    version, nlua, ngo = struct.unpack_from('<HHH', d, 2)
    ncol, nint = struct.unpack_from('<HH', d, 12)
    nodes, refs = struct.unpack_from('<HH', d, 32)
    ntrig, = struct.unpack_from('<H', d, 38)
    p = (156 if version >= 25 else 148) + nlua * 8 + ngo * 92 + ncol * 32 + ntrig * 32
    p = (p + 3) & ~3
    p += nodes * 32 + refs * 4
    p = (p + 3) & ~3
    out = []
    for i in range(nint):
        rad, button, flags, cool, cur, go, name, cosine, reserved = struct.unpack_from('<iBBHHH16shH', d, p + i * size)
        out.append(dict(button=button, flags=flags, go=go, name=name.split(b'\0')[0].decode('latin-1'),
                        cosine=cosine, reserved=reserved))
    return version, out


def fp12cos(deg):
    return round(math.cos(math.radians(deg)) * 4096)


r, d = export(scene(lit={'lineOfSight': True}, far={'lineOfSight': True, 'facingAngle': 60}))
check('export', r.returncode == 0, r.stderr)
version, recs = records(d)
check('version 27', version == 27, version)
check('two interactables', len(recs) == 2, recs)
lit, far = recs
check('record 0 decodes (button, prompt, lineOfSight bit)',
      lit['button'] == 5 and lit['name'] == 'LitPrompt' and lit['flags'] & 4, lit)
check('record 1 starts 32 bytes later', far['button'] == 6 and far['name'] == 'FarPrompt', far)
check('28-byte stride does not decode record 1 (control)', records(d, 28)[1][1]['name'] != 'FarPrompt')
check('default facingAngle 90 -> cosine 0', lit['cosine'] == 0, lit)
check('facingAngle 60 -> 2048', far['cosine'] == 2048 == fp12cos(60), far)
check('reserved bytes are 0', lit['reserved'] == 0 and far['reserved'] == 0, recs)

for deg in (0, 45, 135, 180):
    _, d2 = export(scene(lit={'lineOfSight': True, 'facingAngle': deg}))
    got = records(d2)[1][0]['cosine']
    check('facingAngle %d -> %d' % (deg, fp12cos(deg)), got == fp12cos(deg), got)

_, d3 = export(scene(lit={'lineOfSight': False, 'facingAngle': 30}))
r3 = records(d3)[1][0]
check('lineOfSight off: bit 2 clear, cosine still written', r3['flags'] & 4 == 0 and r3['cosine'] == fp12cos(30), r3)

for bad in (-1, 181):
    rb, _ = export(scene(lit={'facingAngle': bad}))
    check('facingAngle %d is refused' % bad, rb.returncode != 0 and 'facingAngle' in rb.stderr, (rb.returncode, rb.stderr))

src, dst = os.path.join(tmp, 'rs.scene'), os.path.join(tmp, 'rs2.scene')
json.dump(scene(lit={'lineOfSight': True, 'facingAngle': 37.5}), open(src, 'w'))
rr = subprocess.run([cli, 'resave', src, dst], capture_output=True, text=True)
saved = json.load(open(dst)) if rr.returncode == 0 else {}
comps = [c for o in saved.get('objects', []) if o['name'] == 'Lit' for c in o['components'] if c['type'] == 'interactable']
check('resave keeps facingAngle', comps and comps[0].get('facingAngle') == 37.5, (rr.stderr, comps))

print('%d checks, %d failed' % (checks, fails))
sys.exit(1 if fails else 0)
