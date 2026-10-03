# Export tests/ui/ui.scene, read its UI table back the way psxsplash's
# uisystem.cpp does, resolve each element with UISystem::resolveLayout and
# check where it lands on a 320x240 screen. Also checks the font sheets in the
# .vram file and that bad UI input fails the export.
# usage: python3 uicheck.py <splashpack-cli>
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


def export(scene, out):
    return subprocess.run([cli, 'export', scene, '-o', out], capture_output=True, text=True)


def cstr(d, off):
    return d[off:d.index(b'\0', off)].decode() if off else ''


def resolve(e, resw=320, resh=240):  # UISystem::resolveLayout
    ax, ay = (e['amin'][0] * resw) >> 8, (e['amin'][1] * resh) >> 8
    x, y, w, h = ax + e['x'], ay + e['y'], e['w'], e['h']
    if e['amax'][0] != e['amin'][0]: w = ((e['amax'][0] * resw) >> 8) - ax + e['w']
    if e['amax'][1] != e['amin'][1]: h = ((e['amax'][1] * resh) >> 8) - ay + e['h']
    if x < 0: w += x; x = 0
    if y < 0: h += y; y = 0
    w, h = max(w, 1), max(h, 1)
    return x, y, min(w, resw - x), min(h, resh - y)


scene = os.path.join(here, 'ui', 'ui.scene')
out = os.path.join(tmp, 'ui.splashpack')
r = export(scene, out)
check('export', r.returncode == 0, r.stderr)
d = open(out, 'rb').read()
ncanvas, nfont = struct.unpack_from('<HB', d, 92)
table = struct.unpack_from('<I', d, 96)[0]
check('header counts', (ncanvas, nfont) == (1, 2), (ncanvas, nfont))
fonts = []
p = table
for i in range(nfont):
    gw, gh, vx, vy, th, off, size = struct.unpack_from('<BBHHHII', d, p)
    fonts.append(dict(gw=gw, gh=gh, vx=vx, vy=vy, th=th, size=size, adv=d[p + 16:p + 112]))
    p += 112
elements = {}
for ci in range(ncanvas):
    dofs, nlen, sort, count, flags, nofs = struct.unpack_from('<IBBBBI', d, p + ci * 12)
    check('canvas name', cstr(d, nofs) == 'hud', cstr(d, nofs))
    for ei in range(count):
        q = dofs + ei * 48
        typ, eflags, enl, _, enof, x, y, w, h = struct.unpack_from('<BBBBIhhhh', d, q)
        amin, amax = list(d[q + 16:q + 18]), list(d[q + 18:q + 20])
        td = d[q + 24:q + 40]
        text = cstr(d, struct.unpack_from('<I', d, q + 40)[0])
        elements[cstr(d, enof)] = dict(type=typ, x=x, y=y, w=w, h=h, amin=amin, amax=amax, td=td, text=text,
                                       color=tuple(d[q + 20:q + 23]))

expect = {'bar': (0, 220, 320, 20), 'corner': (312, 0, 8, 8), 'crate': (248, 80, 64, 64),
          'title': (8, 8, 300, 20), 'hp': (8, 80, 120, 10)}
for name, rect in expect.items():
    got = resolve(elements[name]) if name in elements else None
    check('rect ' + name, got == rect, got)
line = elements.get('diag')
check('line endpoints past 255', line and struct.unpack_from('<hhhh', line['td']) == (8, 100, 311, 200),
      line and struct.unpack_from('<hhhh', line['td']))
for name, idx in (('title', 1), ('body', 2), ('sys', 0)):
    check('font index ' + name, elements[name]['td'][0] == idx, elements[name]['td'][0])
check('text', elements['title']['text'] == 'SplashEdit NG: UI', elements['title']['text'])
check('progress', tuple(elements['hp']['td'][:4]) == (76, 26, 26, 60), tuple(elements['hp']['td'][:4]))

# Font sheets: inside the x=960 column, one texture page each, not
# overlapping each other or the system font (960, 464, 48 rows).
spans = [(f['vy'], f['vy'] + f['th']) for f in fonts]
check('fonts in column 960', all(f['vx'] == 960 for f in fonts))
check('fonts inside a page', all(a // 256 == (b - 1) // 256 for a, b in spans), spans)
check('fonts clear of each other and the system font',
      all(b <= 464 for a, b in spans) and all(s[1] <= t[0] or t[1] <= s[0] for i, s in enumerate(spans)
                                               for t in spans[i + 1:]), spans)
v = open(out[:-len('splashpack')] + 'vram', 'rb').read()
na, nc, nf = struct.unpack_from('<HHB', v, 2)
q = 8
for i in range(na):
    w, h = struct.unpack_from('<HH', v, q + 4)
    q = (q + 8 + w * h * 2 + 3) & ~3
for i in range(nc):
    n = struct.unpack_from('<H', v, q + 4)[0]
    q = (q + 8 + n * 2 + 3) & ~3
check('vram font count', nf == nfont, nf)
for i in range(nf):
    gw, gh, vx, vy, th, size = struct.unpack_from('<BBHHHI', v, q)
    px = v[q + 12:q + 12 + size]
    check('vram font %d matches descriptor' % i, (gw, gh, vx, vy, th, size) ==
          (fonts[i]['gw'], fonts[i]['gh'], fonts[i]['vx'], fonts[i]['vy'], fonts[i]['th'], fonts[i]['size']))
    # psxsplash writes the font CLUT over the first 8 texels of row 0
    # (psxsplash#56): the row must carry no ink.
    check('vram font %d row 0 blank' % i, size == 128 * th and not any(px[:128]))
    check('vram font %d has ink' % i, any(px))
    q = (q + 12 + size + 3) & ~3

# Bad input fails the export instead of loading wrong.
base = json.load(open(scene))


def bad(name, edit):
    s = json.loads(json.dumps(base))
    edit(s)
    for f in s['fonts']:
        f['source'] = os.path.join(here, 'ui', f['source'])
    for e in s['canvases'][0]['elements']:
        if 'texture' in e: e['texture'] = os.path.join(here, 'ui', e['texture'])
    path = os.path.join(tmp, 'bad.scene')
    json.dump(s, open(path, 'w'))
    r = export(path, os.path.join(tmp, 'bad.splashpack'))
    check('rejects ' + name, r.returncode != 0, r.stdout + r.stderr)
    return r


s = json.loads(json.dumps(base))
s['canvases'][0]['elements'].append({'type': 'sprite', 'name': 'future'})
for f in s['fonts']: f['source'] = os.path.join(here, 'ui', f['source'])
for e in s['canvases'][0]['elements']:
    if 'texture' in e: e['texture'] = os.path.join(here, 'ui', e['texture'])
json.dump(s, open(os.path.join(tmp, 'future.scene'), 'w'))
r = export(os.path.join(tmp, 'future.scene'), os.path.join(tmp, 'future.splashpack'))
fd = open(os.path.join(tmp, 'future.splashpack'), 'rb').read() if r.returncode == 0 else b''
check('unknown element type skipped', r.returncode == 0 and 'future' in r.stderr + r.stdout
      and fd[struct.unpack_from('<I', fd, 96)[0] + 2 * 112 + 6] == 8, r.stdout + r.stderr)
bad('unknown font', lambda s: s['canvases'][0]['elements'][2].update(font='nope'))
bad('four fonts', lambda s: s['fonts'].extend([dict(s['fonts'][1], name='c'), dict(s['fonts'][1], name='d')]))
bad('duplicate font name', lambda s: s['fonts'].append(dict(s['fonts'][1])))
bad('25 canvases', lambda s: s['canvases'].extend([dict(name='c%d' % i, elements=[]) for i in range(24)]))

print('%d checks, %d failed' % (checks, fails))
sys.exit(1 if fails else 0)
