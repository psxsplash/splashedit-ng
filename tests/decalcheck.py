# Decals in the editor, on a copy of the courtyard example with a cyan X
# (textures/xmark.png, transparent around the X) added to the project.
# 1. Dragging xmark.png from the Project panel onto the courtyard floor in the
#    Scene view makes a new object "Decal xmark" with a Decal of it where the
#    pointer meets the floor, its +Z along the floor's normal (up), 1 m wide
#    and as tall as the image (square), selected. Under scripted runs the
#    editor prints
#      decal: placed <name> on <object> at X Y Z normal X Y Z size WxH
#      decal: <name> texture <file> size WxH depth D colour #RRGGBB covers N triangles on M objects pieces P | <object> <n>...
#    The Width typed into the Decal section (obj.dwidth) takes; the scene view
#    draws the X on the floor where it was dropped (cyan pixels there, none
#    before); Ctrl+S saves the component.
# 2. The same drop then Ctrl+Z: one undo step takes the object away.
# 3. Two decals written into the scene: one in mid-air (covers 0 triangles,
#    obj.dnothing warns) and one on top of a crate whose collider is dynamic
#    (the crate is skipped: "| skipped Crate dynamic", obj.dexcluded.Crate).
# 4. splashpack-cli exports the saved scene of run 1 with the decal's pieces
#    as its own triangles, Tri flag 0x8.
# usage: python3 decalcheck.py <splashedit> [keep.png]   (needs a display: xvfb-run -a)
import json, os, re, shutil, struct, subprocess, sys, tempfile, zlib

editor = os.path.abspath(sys.argv[1])
keep = sys.argv[2] if len(sys.argv) > 2 else None
cli = os.path.join(os.path.dirname(editor), 'cli', 'splashpack-cli')
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
fails = 0
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')
W = lambda n: ['--wait', str(n)]
SEARCH = '880,731'      # the Project panel's search field (assetbrowsercheck.py)
TILE = '530,812'        # the first file in its grid
VIEWPORT = (760, 520)   # the courtyard floor in the Scene view


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def write_png(path, w, h, px):
    """px(x, y) -> (r, g, b, a); an 8-bit RGBA PNG."""
    raw = b''.join(b'\0' + bytes(c for x in range(w) for c in px(x, y)) for y in range(h))
    chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def read_png(path):
    """(width, height, rows of RGB bytes) of an 8-bit RGBA or RGB PNG."""
    data = open(path, 'rb').read()
    pos, idat, w, h, bpp = 8, b'', 0, 0, 3
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


def cyan(path, box):
    """Cyan pixels (the decal's X) in the box (x0, y0, x1, y1) of a screenshot: green and blue alike and
    high, red low; the gizmo's light blue arrow (green well under blue) is not."""
    _, _, rows = read_png(path)
    x0, y0, x1, y1 = box
    n = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b = rows[y][3 * x:3 * x + 3]
            n += g > 180 and b > 180 and r < 60 and abs(g - b) < 30
    return n


def project_copy(extra_objects=(), dynamic_crate=False):
    root = tempfile.mkdtemp()
    project = os.path.join(root, 'courtyard')
    shutil.copytree(example, project)
    write_png(os.path.join(project, 'textures', 'xmark.png'), 32, 32,
              lambda x, y: (0, 230, 230, 255) if abs(x - y) <= 3 or abs(x - (31 - y)) <= 3 else (0, 0, 0, 0))
    scene = os.path.join(project, 'courtyard.scene')
    s = json.load(open(scene))
    if dynamic_crate:
        crate = next(o for o in s['objects'] if o['name'] == 'Crates')['children'][0]
        next(c for c in crate['components'] if c['type'] == 'collider')['kind'] = 'dynamic'
    s['objects'] += list(extra_objects)
    json.dump(s, open(scene, 'w'), indent=1)
    return project, scene


def run(project, args, select=None, shot=None):
    shot = shot or os.path.join(os.path.dirname(project), 'shot.png')
    cmd = [editor, '--size', '1600x960', '--project', project] + (['--select', select] if select else []) + \
        W(5) + args + W(10) + ['--screenshot', shot]
    r = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=300)
    lines = (r.stdout + r.stderr).splitlines()
    rects = {}
    for l in lines:
        m = re.match(r'control (\S+(?: \S+)*) at (-?\d+),(-?\d+) (-?\d+),(-?\d+)$', l)
        if m: rects[m.group(1)] = tuple(int(v) for v in m.groups()[1:])
    return r.returncode, [l for l in lines if not l.startswith('control ')], rects, shot


DROP = ['--click', SEARCH + ',0', '--text', 'xmark', '--wait', '30',
        '--drag', TILE + ',%d,%d,0' % VIEWPORT, '--wait', '15']

# 1. Drop, type a width, save.
project, scene = project_copy()
before = os.path.join(os.path.dirname(project), 'before.png')
shot = keep or os.path.join(os.path.dirname(project), 'after.png')
rc, out, rects, shot = run(project, ['--move', '800,300'] + W(3) + ['--shot', before] + DROP +
                           ['--scroll-to', '@obj.dwidth'] + W(5) + ['--dblclick', '@obj.dwidth'] + W(3) +
                           ['--key', 'ctrl+a', '--text', '1.5', '--key', 'enter'] + W(10) +
                           ['--key', 'ctrl+s'] + W(10) + ['--move', '800,300'] + W(5), shot=shot)
check('the editor ran to its screenshot', rc == 0 and os.path.exists(shot), (rc, out[-10:]))
placed = [l for l in out if l.startswith('decal: placed ')]
pm = re.match(r'decal: placed (Decal xmark) on (\S+(?: \S+)*) at (\S+) (\S+) (\S+) normal (\S+) (\S+) (\S+) size (\S+)x(\S+)$',
              placed[0]) if len(placed) == 1 else None
check('the drop places one decal on the floor, facing up, 1 m square', bool(pm) and pm.group(2) == 'Floor' and
      [float(v) for v in pm.groups()[5:8]] == [0, 1, 0] and pm.group(9) == '1.00' and pm.group(10) == '1.00', placed)
check('the new decal object is selected', 'selected: Decal xmark' in out, [l for l in out if l.startswith('selected:')])
lines = [l for l in out if l.startswith('decal: Decal xmark ')]
cm = [re.match(r'decal: Decal xmark texture xmark.png size (\S+)x(\S+) depth 0.5 colour #FFFFFF covers (\d+) triangles on (\d+) objects '
               r'pieces (\d+) \| Floor (\d+)$', l) for l in lines]
check('the inspector covers the floor, then at the typed width', len(cm) == 2 and all(cm) and
      [m.group(1) for m in cm] == ['1', '1.5'] and all(int(m.group(3)) > 0 and m.group(4) == '1' for m in cm) and
      int(cm[1].group(5)) >= int(cm[0].group(5)), lines)
check('the Decal section shows Texture, Width, Height, Depth, Colour and the cover line',
      all(k in rects for k in ('obj.dtexture', 'obj.dwidth', 'obj.dheight', 'obj.ddepth', 'obj.dcol', 'obj.dcovers')) and
      'obj.dnothing' not in rects, sorted(k for k in rects if k.startswith('obj.d')))
box = (VIEWPORT[0] - 70, VIEWPORT[1] - 45, VIEWPORT[0] + 70, VIEWPORT[1] + 45)
if os.path.exists(before) and os.path.exists(shot):
    c0, c1 = cyan(before, box), cyan(shot, box)
    check('the scene view draws the X on the floor where it was dropped', c0 == 0 and c1 > 100, (c0, c1))
saved = json.load(open(scene))
obj = next((o for o in saved['objects'] if o['name'] == 'Decal xmark'), None)
dec = next((c for c in obj['components'] if c['type'] == 'decal'), None) if obj else None
check('Ctrl+S saves the decal component with the typed width', bool(dec) and dec['texture'] == 'textures/xmark.png' and
      abs(dec['width'] - 1.5) < 1e-6 and abs(dec['height'] - 1.0) < 1e-6, dec)
pack = os.path.join(os.path.dirname(project), 'out.splashpack')
e = subprocess.run([cli, 'export', scene, '-o', pack], capture_output=True, text=True)
check('splashpack-cli exports it', e.returncode == 0, (e.stdout + e.stderr)[-500:])
if e.returncode == 0 and cm and cm[-1]:
    d = open(pack, 'rb').read()
    # Tri flags at +50 of each 52-byte Tri: count the decal ones (0x8, with the cutout 0x2) across the mesh data.
    flagged = sum(1 for at in range(0, len(d) - 52, 4) if struct.unpack_from('<H', d, at + 50)[0] & 0xfffb == 0x0a and
                  struct.unpack_from('<H', d, at + 44)[0] & 0x200)
    check('the pack has the decal pieces with Tri flag 0x8', flagged >= int(cm[-1].group(5)), (flagged, cm[-1].group(5)))

# 2. Drop, then Ctrl+Z: one undo step.
project2, scene2 = project_copy()
rc, out, rects, _ = run(project2, DROP + ['--key', 'ctrl+z'] + W(5) + ['--key', 'ctrl+s'] + W(10))
saved = json.load(open(scene2))
check('one Ctrl+Z takes the dropped decal away', any(l.startswith('decal: placed ') for l in out) and
      not any(o['name'].startswith('Decal') for o in saved['objects']), [o['name'] for o in saved['objects']])

# 3. Nothing under it; a dynamic crate under it.
down = [-0.70710678, 0, 0, 0.70710678]  # +Z to world +Y: projecting down


def decal_obj(name, pos, size):
    return {'name': name, 'active': True, 'transform': {'position': pos, 'rotation': down, 'scale': [1, 1, 1]},
            'components': [{'type': 'decal', 'texture': 'textures/xmark.png', 'width': size, 'height': size, 'depth': 0.5,
                            'color': [1, 1, 1]}], 'children': []}


project3, _ = project_copy([decal_obj('Decal Air', [0, 6, -3], 1), decal_obj('Decal Crate', [0, 1.0, 1.0], 0.5)], dynamic_crate=True)
rc, out, rects, _ = run(project3, ['--scroll-to', '@obj.dcovers'] + W(5), select='Decal Air')
air = [l for l in out if l.startswith('decal: Decal Air ')]
check('a decal in mid-air covers nothing and says so',
      air == ['decal: Decal Air texture xmark.png size 1x1 depth 0.5 colour #FFFFFF covers 0 triangles on 0 objects pieces 0'] and
      'obj.dnothing' in rects, (air, sorted(k for k in rects if k.startswith('obj.d'))))
rc, out, rects, _ = run(project3, ['--scroll-to', '@obj.dcovers'] + W(5), select='Decal Crate')
crate = [l for l in out if l.startswith('decal: Decal Crate ')]
check('a decal over a dynamic crate skips it and warns',
      len(crate) == 1 and crate[0].endswith('covers 0 triangles on 0 objects pieces 0 | skipped Crate dynamic') and
      'obj.dexcluded.Crate' in rects, (crate, sorted(k for k in rects if k.startswith('obj.d'))))

print('decalcheck: %s' % ('all passed' if not fails else '%d failed' % fails))
sys.exit(1 if fails else 0)
