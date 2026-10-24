# Particle emitters in the editor (splashedit-ng#196), driven through the GUI
# on a project made from tests/particles: a sprite sheet 'spark' (four 8x8
# cells, animation 'twinkle'), 'Fountain' (looping, autoplay, 24 a second for
# 1.5 s, pool 40, still cell 1, warm tint), 'Sparks' (one-shot, twinkle, pool
# 48), a crate and an empty 'Spare'. The inspector's Particle emitter section
# names its controls obj.pt*; the Scene view plays every emitter from the
# start and names the drawn quads' bounds vp.ptbox.<name>. Under scripted runs
# the editor prints
#   particles: <name> sheet S anim A cell C rate R burst B life L vel x,y,z spread x,y,z gravity G start #rrggbb end #rrggbb size S max M loop on|off autoplay on|off pool P
#   particle preview: <name> tick 120 live N drawn D
#   statusbar: particles emitters E max M pool P bump B ram R bumpsize S
# and the status bar's RAM line. Every run saves with Ctrl+S; the check reads
# the saved scene and exports it with splashpack-cli to compare the meters
# with what the export writes.
# usage: python3 particlescheck.py <splashedit>   (needs a display: xvfb-run -a)
import json, os, re, shutil, struct, subprocess, sys, tempfile, zlib

editor = os.path.abspath(sys.argv[1])
cli = os.path.join(os.path.dirname(editor), 'cli', 'splashpack-cli')
here = os.path.dirname(os.path.abspath(__file__))
src = os.path.join(here, 'particles')
courtyard = os.path.join(here, '..', 'examples', 'courtyard')
fails = 0
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')
W = lambda n: ['--wait', str(n)]


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def png_rgb(path):
    """(width, height, rows of RGB bytes) of an 8-bit RGBA or RGB PNG."""
    data = open(path, 'rb').read()
    pos, idat, w = 8, b'', 0
    while pos < len(data):
        n, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
            bpp = 4 if ctype == 6 else 3
        elif kind == b'IDAT':
            idat += body
        pos += 12 + n
    raw, stride, rows, prev = zlib.decompress(idat), w * bpp, [], bytearray(w * bpp)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b, c = prev[i], prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line[i] for i in range(stride) if i % bpp < 3))
        prev = line
    return w, h, rows


def fixture(edit=None):
    project = os.path.join(tempfile.mkdtemp(), 'pt')
    os.makedirs(os.path.join(project, 'meshes'))
    os.makedirs(os.path.join(project, 'textures'))
    for f in ('spark.png', 'particles.lua', 'crate.lua'):
        shutil.copy(os.path.join(src, f), project)
    shutil.copy(os.path.join(courtyard, 'meshes', 'crate.mesh'), os.path.join(project, 'meshes'))
    shutil.copy(os.path.join(courtyard, 'textures', 'crate.png'), os.path.join(project, 'textures'))
    s = json.load(open(os.path.join(src, 'particles.scene'), encoding='utf-8'))
    for o in s['objects']:
        for c in o['components']:
            if c['type'] == 'mesh':
                c['mesh'] = 'meshes/crate.mesh'
                c['materials'][0]['texture'] = 'textures/crate.png'
    s['objects'].append({'name': 'Spare', 'active': True, 'transform': {'position': [0, 0, 6], 'rotation': [0, 0, 0, 1],
                         'scale': [1, 1, 1]}, 'components': [], 'children': []})
    if edit: edit(s)
    json.dump(s, open(os.path.join(project, 'particles.scene'), 'w', encoding='utf-8'), indent=1)
    return project


def run(name, select, args, edit=None):
    """The editor on a fresh fixture with `select` selected, its first export
    done and the preview past two seconds, then `args`, Ctrl+S and a
    screenshot; returns the saved scene's objects by name, the lines, the
    named rects, the shot and the project."""
    project = fixture(edit)
    shot = os.path.join(project, name + '.png')
    cmd = [editor, '--size', '1600x960', '--project', project, '--scene', os.path.join(project, 'particles.scene'),
           '--select', select] + W(20) + ['--wait-export'] + W(150) + args + ['--key', 'ctrl+s'] + W(10) + \
        ['--wait-export'] + W(5) + ['--move', '760,940'] + W(5) + ['--screenshot', shot]
    r = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=280)
    lines = (r.stdout + r.stderr).strip().splitlines()
    missing = [l for l in lines if 'no such control drawn' in l]
    check(name + ': every control the script names was drawn', not missing, missing)
    rects = {}
    for l in lines:
        m = re.match(r'control (\S+) at (-?\d+),(-?\d+) (-?\d+),(-?\d+)$', l)
        if m: rects[m.group(1)] = tuple(int(v) for v in m.groups()[1:])
    s = json.load(open(os.path.join(project, 'particles.scene'), encoding='utf-8'))
    objs = {o['name']: o for o in s['objects']}
    return objs, lines, rects, shot, project


def comp(objs, name, kind='particles'):
    return next((c for c in objs.get(name, {}).get('components', []) if c.get('type') == kind), None)


def typed(name, text):
    return ['--scroll-to', '@' + name] + W(3) + ['--dblclick', '@' + name] + W(3) + ['--key', 'ctrl+a', '--text', text,
                                                                                     '--key', 'enter'] + W(3)


def click(name):
    return (['--scroll-to', '@' + name] + W(3) if '/' not in name else []) + ['--click', '@' + name] + W(4)


def warm(px, box, w, h):
    """Pixels inside box (x0, y0, x1, y1) in the Fountain's warm colours."""
    n = 0
    for y in range(max(0, box[1]), min(h, box[3])):
        row = px[y]
        for x in range(max(0, box[0]), min(w, box[2])):
            r, g, b = row[3 * x], row[3 * x + 1], row[3 * x + 2]
            if r > 200 and g > 90 and b < 140 and r - b > 90:
                n += 1
    return n


def export_stats(project):
    out = os.path.join(project, 'out.splashpack')
    r = subprocess.run([cli, 'export', os.path.join(project, 'particles.scene'), '-o', out, '--stats'],
                       capture_output=True, text=True, timeout=120)
    return r.returncode, r.stdout + r.stderr


def last(lines, prefix):
    hits = [l for l in lines if l.startswith(prefix)]
    return hits[-1] if hits else ''


# 1. Shown: the section, its line, the preview, the meter equal to the export.
objs, lines, rects, shot, project = run('shown', 'Fountain', [])
names = ('s_particles', 'obj.ptsheet', 'obj.ptanim', 'obj.ptcell', 'obj.ptrate', 'obj.ptburst', 'obj.ptlife', 'obj.ptvel.y',
         'obj.ptspread.x', 'obj.ptgrav', 'obj.ptstart', 'obj.ptend', 'obj.ptsize', 'obj.ptmax', 'obj.ptloop', 'obj.ptauto',
         'obj.ptcost', 'obj.ptlua')
check('the section and its controls are drawn', all(n in rects for n in names), [n for n in names if n not in rects])
check('the particles line reports the saved emitter',
      last(lines, 'particles: Fountain') == 'particles: Fountain sheet spark anim - cell 1 rate 24 burst 0 life 1.5 vel 0,2.6,0 '
      'spread 0.7,0.4,0.4 gravity 3.5 start #ffcc80 end #590d00 size 0.3 max 40 loop on autoplay on pool 1156',
      last(lines, 'particles: Fountain'))
# 24 a second for 1.5 s: at tick 120 the pool holds the last 90 ticks' 36 (core/particles.hh, as the console).
pv = last(lines, 'particle preview: Fountain')
m = re.match(r'particle preview: Fountain tick 120 live (\d+) drawn (\d+)$', pv)
check('the preview has played 36 Fountain particles at tick 120, as the console does', m and m.group(1) == '36', pv)
check('and draws them', m and int(m.group(2)) > 20, pv)
check('the preview names their bounds and the emitter', 'vp.ptbox.Fountain' in rects and 'vp.particles.Fountain' in rects,
      [k for k in rects if k.startswith('vp.p')])
w, h, px = png_rgb(shot)
box = rects.get('vp.ptbox.Fountain', (0, 0, 0, 0))
hot = warm(px, box, w, h)
print('warm pixels in the Fountain quads\' bounds %s: %d' % (box, hot))
check('the quads are drawn in the Fountain\'s warm tint', hot > 40, hot)
bar = last(lines, 'statusbar: particles')
ram = last(lines, 'statusbar: ram ')
rc, out = export_stats(project)
cl = re.search(r'^particles emitters=(\d+) max=(\d+) pool=(\d+) bump=(\d+)$', out, re.M)
cr = re.search(r'^ram=(\d+) ordering_table=\d+ bump=(\d+)$', out, re.M)
print('meter: %s | %s\nexport: %s %s' % (bar, ram, cl and cl.group(0), cr and cr.group(0)))
check('the export of the saved scene succeeds', rc == 0 and cl and cr, out[-400:])
if cl and cr:
    check('the Particles meter is what the export writes: 2 emitters, 88 particles, pool 2536, 3520 bytes of quads',
          bar.startswith('statusbar: particles emitters %s max %s pool %s bump %s ram %s bumpsize %s' %
                         (cl.group(1), cl.group(2), cl.group(3), cl.group(4), cr.group(1), cr.group(2))) and
          cl.groups() == ('2', '88', '2536', '3520'), bar)
    check('the RAM meter is the export\'s', ram.startswith('statusbar: ram %s/' % cr.group(1)), ram)

# 2. The negative arm: Fountain off (its object inactive, still selected so the view frames it the
# same) draws nothing where it did, and the meter drops it.
objs, lines, rects2, shot2, project2 = run('off', 'Fountain', [], edit=lambda s: [o.update(active=False) for o in s['objects']
                                                                               if o['name'] == 'Fountain'])
w2, h2, px2 = png_rgb(shot2)
cold = warm(px2, box, w2, h2)
print('warm pixels in the same bounds with Fountain off: %d' % cold)
# The crate's lit edges have a few warm texels of their own: under 1% of what the quads gave.
check('with Fountain inactive nothing warm is drawn there', cold * 100 < hot, (cold, hot))
check('nor are its bounds named', 'vp.ptbox.Fountain' not in rects2, [k for k in rects2 if k.startswith('vp.p')])
check('and the meter counts Sparks alone', last(lines, 'statusbar: particles').startswith(
      'statusbar: particles emitters 1 max 48 pool 1380 bump 1920 '), last(lines, 'statusbar: particles'))

# 3. Edits: rate, pool, the animation, looping off (Duration appears), autoplay off; the meter follows.
EDIT = typed('obj.ptrate', '30') + typed('obj.ptmax', '50') + click('obj.ptanim') + click('ptanim/twinkle') + \
    click('obj.ptloop') + typed('obj.ptdur', '2') + click('obj.ptauto')
objs, lines, rects, shot, project = run('edit', 'Fountain', EDIT)
pe = comp(objs, 'Fountain') or {}
check('rate, pool, animation, looping, duration and autoplay are saved',
      pe.get('rate') == 30 and pe.get('maxParticles') == 50 and pe.get('anim') == 'twinkle' and pe.get('loop') is False and
      pe.get('duration') == 2 and pe.get('autoplay') is False, pe)
check('the line follows', last(lines, 'particles: Fountain').startswith('particles: Fountain sheet spark anim twinkle ') and
      ' rate 30 ' in last(lines, 'particles: Fountain') and last(lines, 'particles: Fountain').endswith(
          ' max 50 loop off autoplay off pool 1436'), last(lines, 'particles: Fountain'))
check('the meter takes the bigger pool', last(lines, 'statusbar: particles').startswith(
      'statusbar: particles emitters 2 max 98 pool 2816 bump 3920 '), last(lines, 'statusbar: particles'))
objs, lines, rects, shot, project = run('undo', 'Fountain', typed('obj.ptrate', '5') + ['--key', 'ctrl+z'] + W(4))
check('Ctrl+Z puts the rate back', (comp(objs, 'Fountain') or {}).get('rate') == 24, comp(objs, 'Fountain'))

# 4. The picker adds an emitter to Spare; its sheet is picked from the scene's.
objs, lines, rects, shot, project = run('picker', 'Spare', ['--click', '@obj.addcomp'] + W(3) + ['--text', 'particle'] + W(2) +
                                        ['--key', 'enter'] + W(5) + click('obj.ptsheet') + click('ptsheet/spark'))
pe = comp(objs, 'Spare') or {}
check('the picker adds a particle emitter with its defaults and the picked sheet',
      pe.get('sheet') == 'spark' and pe.get('maxParticles') == 32 and pe.get('rate') == 10 and pe.get('loop') is True, pe)
check('the meter counts three emitters', last(lines, 'statusbar: particles').startswith(
      'statusbar: particles emitters 3 max 120 '), last(lines, 'statusbar: particles'))

print('%d failure%s' % (fails, '' if fails == 1 else 's'))
sys.exit(1 if fails else 0)
