# The mesh inspector's Palette section, driven through the GUI on a courtyard copy (the Crate's
# crate.png quantizes to 8 entries): edit entry 3 in the picker and set its STP bit, take the
# palette from palette/eight.gpl, drag across swatches 1-4 to make a cycle, type its step time,
# undo that with Ctrl+Z, refuse an overlapping drag, save. The sidecar crate.png.palette.json must
# say exactly that, and splashpack-cli must export the CLUT record psxsplash's PaletteCycler reads:
# the .gpl words with entry 3 replaced, one range {1, 4, 768 = 0.1 s, +1}. A second run starts
# from that sidecar and checks the section reads it back; a third, with a cycle over the entries
# the crate draws, checks the Scene view redraws it as the range steps.
# usage: python3 palettecheck.py <splashedit> <splashpack-cli> [shot dir]   (needs a display: xvfb-run -a)
import json, os, re, shutil, struct, subprocess, sys, tempfile

editor = os.path.abspath(sys.argv[1])
cli = os.path.abspath(sys.argv[2])
shot_dir = sys.argv[3] if len(sys.argv) > 3 else None
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import splashdiff  # noqa: E402

example = os.path.join(here, '..', 'examples', 'courtyard')
fixture = os.path.join(here, 'palette', 'eight.gpl')
fails = 0
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')
SIDECAR = os.path.join('textures', 'crate.png.palette.json')


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def word(r, g, b, stp=False):
    """An 8-bit colour as the export stores it: 5-bit channels rounded to nearest, black 0x8000."""
    c5 = lambda c: int(c / 255 * 31 + 0.5)
    w = (c5(b) << 10) | (c5(g) << 5) | c5(r)
    return (w | 0x8000) if (stp or w == 0) else w


def hexword(h, stp=False):
    return word(int(h[1:3], 16), int(h[3:5], 16), int(h[5:7], 16), stp)


def project():
    p = os.path.join(tempfile.mkdtemp(), 'courtyard')
    shutil.copytree(example, p)
    os.makedirs(os.path.join(p, 'palettes'))
    shutil.copy(fixture, os.path.join(p, 'palettes', 'eight.gpl'))
    return p


def run(proj, args, shot):
    """Editor on `proj` with the Crate selected and the export done, then `args`. Returns the
    named rects, the `palette:` and `viewport: cycling` lines, and the whole output."""
    cmd = [editor, '--size', '1600x960', '--project', proj, '--select', 'Crate', '--wait', '5', '--wait-export',
           '--scroll-to', '@pal.grid', '--wait', '3'] + args + ['--move', '760,940', '--wait', '5', '--screenshot', shot]
    r = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=300)
    lines = (r.stdout + r.stderr).strip().splitlines()
    missing = [l for l in lines if 'no such control drawn' in l]
    check('every control the script names was drawn', not missing, missing)
    check('the editor exits 0', r.returncode == 0, lines[-5:])
    rects = {}
    for l in lines:
        m = re.match(r'control (\S+(?: \S+)*) at (-?\d+),(-?\d+) (-?\d+),(-?\d+)$', l)
        if m: rects[m.group(1)] = tuple(int(v) for v in m.groups()[1:])
    pal = [l for l in lines if l.startswith('palette: ')]
    vp = [l for l in lines if l.startswith('viewport: cycling')]
    return rects, pal, vp, '\n'.join(l for l in lines if not l.startswith('control '))


def typed(name, text):
    return ['--scroll-to', '@' + name, '--dblclick', '@' + name, '--wait', '3', '--key', 'ctrl+a', '--text', text,
            '--key', 'enter', '--wait', '5']


# ---- Probe: the section as it opens, and the swatch pitch for the drags.
proj = project()
rects, pal, vp, out = run(proj, [], os.path.join(proj, 'probe.png'))
check('the section shows the quantized 8-entry palette, nothing edited',
      'palette: textures/crate.png 8bpp source quantize entries 8 edited none cycles none' in pal, pal)
check('no sidecar before an edit', not os.path.exists(os.path.join(proj, SIDECAR)))
check('the shared note names the two other crates', 'pal.shared' in rects)
check('the Scene view cycles nothing yet', vp == ['viewport: cycling none'], vp)
s1, s2 = rects.get('pal.swatch.1'), rects.get('pal.swatch.2')
pitch = (s2[0] - s1[0]) if s1 and s2 else 19
check('swatches sit in a row, at least 8 px apart', s1 and s2 and s2[1] == s1[1] and pitch >= 8, (s1, s2))
if fails:
    sys.exit(1)

# ---- The edits.
proj = project()
palshot = os.path.join(proj, 'palette.png')
args = (['--click', '@pal.swatch.3', '--wait', '5', '--click', '@pal.entrypick,40,-40', '--wait', '5', '--key', 'escape',
         '--wait', '3', '--click', '@pal.stp', '--wait', '5',
         '--click', '@pal.source/From file', '--wait', '5', '--click', '@pal.files/palettes/eight.gpl', '--wait', '10',
         '--scroll-to', '@pal.grid', '--drag', '@pal.swatch.1,0,0,%d,0' % (3 * pitch), '--wait', '5'] +
        typed('pal.cycle.0.step', '0.25') +
        ['--scroll-to', '@pal.grid', '--wait', '3', '--shot', palshot,
         '--key', 'ctrl+z', '--wait', '5',
         '--scroll-to', '@pal.grid', '--drag', '@pal.swatch.2,0,0,%d,0' % (2 * pitch), '--wait', '5',
         '--key', 'ctrl+s', '--wait', '10'])
rects, pal, vp, out = run(proj, args, os.path.join(proj, 'edits.png'))
edited = [re.search(r'edited (3=#[0-9a-f]{6}s?) ', l) for l in pal]
edited = [m.group(1) for m in edited if m]
check('the picker sets entry 3, then STP sets its bit', len(edited) >= 2 and not edited[0].endswith('s') and edited[-1].endswith('s'),
      pal)
hex3 = edited[-1][2:9] if edited else '#000000'
check('the colour picked is not the quantized one left as it was', hex3 != '#000000')
want_line = 'palette: textures/crate.png 8bpp source file ../palettes/eight.gpl entries 8 edited 3=%ss cycles ' % hex3
check('From file takes eight.gpl, keeping the edited entry', any(l.startswith(want_line) for l in pal), pal)
check('the drag across swatches 1-4 makes a cycle of 0.1 s', want_line + '1+4/768+' in pal, pal)
check('typing 0.25 s stores period 1920', want_line + '1+4/1920+' in pal, pal)
i1920 = pal.index(want_line + '1+4/1920+') if want_line + '1+4/1920+' in pal else -1
check('Ctrl+Z puts the step back to 0.1 s', i1920 >= 0 and want_line + '1+4/768+' in pal[i1920:], pal)
check('the overlapping drag is refused, saying why',
      any('palette: message Entries 2-4 overlap the cycle on 1-4' in l for l in pal), pal)
check('the save leaves nothing unsaved', 'unsaved changes: no' in out, out[-400:])
check('the Scene view cycles the crate', any('textures/crate.png 8bpp' in l for l in vp), vp)

side = os.path.join(proj, SIDECAR)
got = json.load(open(side, encoding='utf-8')) if os.path.exists(side) else None
want = {'version': 1, 'source': 'file', 'file': '../palettes/eight.gpl', 'entries': {'3': {'color': hex3, 'stp': True}},
        'cycles': [{'first': 1, 'count': 4, 'stepSeconds': 0.1, 'direction': 1}]}
check('the sidecar says exactly that', got == want, got)

# ---- The export: the CLUT record PaletteCycler reads.
pack = os.path.join(proj, 'out.splashpack')
r = subprocess.run([cli, 'export', os.path.join(proj, 'courtyard.scene'), '-o', pack], capture_output=True, text=True)
check('splashpack-cli exports the scene', r.returncode == 0, (r.stdout + r.stderr)[-300:])
gpl = [(0, 0, 0), (255, 255, 255), (200, 40, 40), (40, 200, 40), (40, 40, 200), (200, 200, 40), (40, 200, 200), (140, 90, 50)]
words = [word(*c) for c in gpl]
words[3] = hexword(hex3, True)
clut = struct.pack('<8H', *words)
if r.returncode == 0:
    p = splashdiff.Pack(pack)
    check('the .vram holds the CLUT: the .gpl words with entry 3 replaced', any(c['data'] == clut for c in p.cluts),
          [c['data'].hex() for c in p.cluts])
    rec = struct.pack('<HBB', 8, 1, 0) + clut + struct.pack('<BBHbB', 1, 4, 768, 1, 0)
    at = p.d.find(rec)
    check('the pack holds the record: header, the 8 words, range {1, 4, 768, +1}', at > 0 and p.d.find(rec, at + 1) < 0, at)
    check('a CLUT table entry flags it (pad bit 0) and points at it',
          at > 0 and at % 4 == 0 and re.search(re.escape(struct.pack('<I', at)) + b'....' + struct.pack('<HH', 8, 1), p.d, re.S) is not None)

# ---- From the saved sidecar: read back, and the Scene view cycling.
rects, pal, vp, out = run(proj, [], os.path.join(proj, 'reopen.png'))
check('reopened, the section reads the sidecar back', any(l.startswith(want_line + '1+4/768+') for l in pal), pal)
check('reopened, the Scene view cycles the crate', any('textures/crate.png 8bpp' in l for l in vp), vp)
# On eight.gpl the crate's texels take entries outside 1-4, so that cycle shows no change. A cycle
# over entries 0-6 of the quantized palette (all of them drawn) does; of three shots at least one
# differs whatever the frame rate, unless the view stands still.
json.dump({'version': 1, 'cycles': [{'first': 0, 'count': 7, 'stepSeconds': 0.25}]}, open(side, 'w', encoding='utf-8'))
shots = [os.path.join(proj, 'scene-%s.png' % c) for c in 'abc']
rects, pal, vp, out = run(proj, ['--shot', shots[0], '--wait', '15', '--shot', shots[1], '--wait', '25', '--shot', shots[2]],
                          os.path.join(proj, 'cycling.png'))
check('the Scene view cycles the quantized crate', any('textures/crate.png 8bpp' in l for l in vp), vp)
seen = [open(f, 'rb').read() for f in shots if os.path.exists(f)]
check('the Scene view changes as the range steps', len(seen) == 3 and len(set(seen)) > 1, len(set(seen)))

if shot_dir:
    os.makedirs(shot_dir, exist_ok=True)
    for f in [palshot, os.path.join(proj, 'edits.png')] + shots:
        if os.path.exists(f): shutil.copy(f, shot_dir)
print(f'{fails} failures')
sys.exit(1 if fails else 0)
