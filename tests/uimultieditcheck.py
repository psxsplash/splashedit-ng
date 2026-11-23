# Editing several UI elements at once (#364), driven through the GUI on a copy
# of tests/ui/ui.scene. With several selected the inspector shows each field
# they share: its value when they agree, "mixed" when they differ. An edit
# sets it on all of them, one undo step. The three texts (title, body, sys)
# have three colours and fonts; Paste on their Colour well gives all three
# the copied colour, and one Ctrl+Z puts every one back. Fields of one type
# (Font) show only when all are of that type, Blend only when all can blend.
# usage: python3 uimultieditcheck.py <splashedit> [keep-dir]   (needs a display: xvfb-run -a)
# keep-dir, when given, receives texts-mixed.png and texts-pasted.png.
import json, os, re, shutil, subprocess, sys, tempfile

editor = os.path.abspath(sys.argv[1])
keep = sys.argv[2] if len(sys.argv) > 2 else None
here = os.path.dirname(os.path.abspath(__file__))
fails = 0
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')
W = ['--wait', '3']


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def project():
    """tests/ui/ui.scene as a project of its own, fonts and texture copied in; 'sys' hidden."""
    d = os.path.join(tempfile.mkdtemp(), 'ui')
    os.makedirs(os.path.join(d, 'fonts'))
    os.makedirs(os.path.join(d, 'textures'))
    s = json.load(open(os.path.join(here, 'ui', 'ui.scene'), encoding='utf-8'))
    for f in s['fonts']:
        shutil.copy(os.path.join(here, 'ui', f['source']), os.path.join(d, 'fonts'))
        f['source'] = 'fonts/' + os.path.basename(f['source'])
    for e in s['canvases'][0]['elements']:
        if 'texture' in e:
            shutil.copy(os.path.join(here, 'ui', e['texture']), os.path.join(d, 'textures'))
            e['texture'] = 'textures/' + os.path.basename(e['texture'])
        if e['name'] == 'sys': e['visible'] = False
    json.dump(s, open(os.path.join(d, 'ui.scene'), 'w'), indent=1)
    return d


ORIG = None  # the elements as a plain save writes them


def run(args, shot=None):
    """A fresh project with the UI tab open, `args`, then Ctrl+S; returns the
    saved elements by name, the output (without the controls) and the named
    controls of the last frame."""
    d = project()
    scene = os.path.join(d, 'ui.scene')
    cmd = [editor, '--size', '1600x960', '--project', d, '--ui', '--wait', '5'] + args + [
        '--key', 'ctrl+s', '--wait', '5', '--screenshot', shot or os.path.join(d, 'shot.png')]
    r = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=180)
    lines = (r.stdout + r.stderr).strip().splitlines()
    missing = [l for l in lines if 'no such control drawn' in l]
    check('every control the script names was drawn', not missing, missing)
    rects = {}
    for l in lines:
        m = re.match(r'control (\S+) at (-?\d+),(-?\d+) (-?\d+),(-?\d+)$', l)
        if m: rects[m.group(1)] = tuple(int(v) for v in m.groups()[1:])
    out = '\n'.join(l for l in lines if not l.startswith('control '))
    els = {e['name']: e for e in json.load(open(scene, encoding='utf-8'))['canvases'][0]['elements']}
    return els, out, rects


def shown(out, field):
    """What the multi inspector last reported for `field`."""
    v = None
    for l in out.splitlines():
        if l.startswith('uicanvas: multi ' + field + ' '): v = l[len('uicanvas: multi ' + field + ' '):]
    return v


def centre(r, button=None):
    s = '%d,%d' % ((r[0] + r[2]) // 2, (r[1] + r[3]) // 2)
    return s if button is None else s + ',%d' % button


def changed(els):
    return sorted(n for n in ORIG if els.get(n) != ORIG[n])


ORIG, out, _ = run([])
m = re.search(r'uicanvas: screen [\d.]+,[\d.]+ scale [\d.]+ list ([\d.]+),([\d.]+) row ([\d.]+)', out)
check('--ui reports where the element list is', m is not None, out[-300:])
if not m:
    print('FAILS=%d' % fails)
    sys.exit(1)
LX, LY, LH = map(float, m.groups())


def row(i):
    return '%d,%d' % (round(LX + 40), round(LY + (i + 0.5) * LH))


def select(*rows_):
    """A plain click on the first row, Ctrl+click on the others."""
    out = ['--click', row(rows_[0])] + W + ['--keydown', 'ctrl'] + W
    for i in rows_[1:]: out += ['--click', row(i)] + W
    return out + ['--keyup', 'ctrl'] + W


BAR, CORNER, TITLE, BODY, SYS, HP = range(6)
TEXTS = select(TITLE, BODY, SYS)

# Three texts: colour, font and visible differ; Font shows, Blend does not
# (one is in the system font, which cannot blend).
els, out, rects = run(TEXTS, shot=keep and os.path.join(keep, 'texts-mixed.png'))
check('three texts selected: the multi inspector', 'uicanvas: inspector 3 elements' in out, out[-300:])
check('their differing colours read "mixed"', shown(out, 'col') == 'mixed', shown(out, 'col'))
check('their differing fonts read "mixed"', shown(out, 'font') == 'mixed', shown(out, 'font'))
check('two shown, one hidden: visible reads "mixed"', shown(out, 'visible') == 'mixed', shown(out, 'visible'))
check('all texts: the Font row is drawn', 'ui.font' in rects, sorted(k for k in rects if k.startswith('ui.')))
# Judged on the last frame: title and body, selected first, do blend and
# report a Blend on the way.
check('a system-font text among them: no Blend row', 'ui.blend' not in rects, sorted(k for k in rects if k.startswith('ui.')))
check('their heights differ: the H field reads "mixed"', shown(out, 'sizeb') == 'mixed', shown(out, 'sizeb'))
check('their widths agree: the W field reads 300', shown(out, 'sizea') == '300', shown(out, 'sizea'))
check('just selecting changes nothing', changed(els) == [], changed(els))
if 'ui.col' not in rects:
    print('FAILS=%d' % fails)
    sys.exit(1)
COL = centre(rects['ui.col'], 1)
_, out, one = run(['--click', row(CORNER)] + W)
check("corner alone: the usual inspector's Colour", 'uicanvas: inspector element corner' in out and 'ui.col' in one, out[-300:])
if 'ui.col' not in one:
    print('FAILS=%d' % fails)
    sys.exit(1)

# Copy corner's colour (#FF0000), Paste it on the three texts: all of them get it.
COPY = ['--click', row(CORNER)] + W + ['--click', centre(one['ui.col'], 1)] + W + ['--click', '@ui.col.menu/Copy'] + W
PASTE = ['--click', COL] + W + ['--click', '@ui.col.menu/Paste'] + W
els, out, rects = run(COPY + TEXTS + PASTE, shot=keep and os.path.join(keep, 'texts-pasted.png'))
red = ORIG['corner']['color']
check('Paste on the mixed Colour sets it on all three texts',
      all(els[n]['color'] == red for n in ('title', 'body', 'sys')), [els[n]['color'] for n in ('title', 'body', 'sys')])
check('nothing else changes', changed(els) == ['body', 'sys', 'title'], changed(els))
check('the Colour then reads the one colour', shown(out, 'col') == '#FF0000', shown(out, 'col'))
els, out, rects = run(COPY + TEXTS + PASTE + ['--key', 'ctrl+z'] + W)
check('one Ctrl+Z puts all three colours back', changed(els) == [], changed(els))

# The font menu: one pick sets the font of all three.
els, out, rects = run(TEXTS + ['--click', '@ui.font'] + W + ['--click', '@ui.font/title'] + W)
check('a Font pick sets it on all three', all(els[n].get('font') == 'title' for n in ('title', 'body', 'sys')),
      [els[n].get('font') for n in ('title', 'body', 'sys')])
check('the Font then reads it', shown(out, 'font') == 'title', shown(out, 'font'))

# The mixed visible switch shows them all; one Ctrl+Z hides sys again.
els, out, rects = run(TEXTS + ['--click', '@ui.visible'] + W)
check('a click on the mixed Visible switch shows all three', all(els[n].get('visible', True) for n in ('title', 'body', 'sys')) and
      shown(out, 'visible') == 'on', ([els[n].get('visible') for n in ('title', 'body', 'sys')], shown(out, 'visible')))
els, out, rects = run(TEXTS + ['--click', '@ui.visible'] + W + ['--key', 'ctrl+z'] + W)
check('one Ctrl+Z hides sys again', changed(els) == [], changed(els))

# A typed H on the three: each gets it.
els, out, rects = run(TEXTS + ['--dblclick', '@ui.sizeb'] + W + ['--key', 'ctrl+a', '--text', '24', '--key', 'enter'] + W)
check('a typed H on the mixed field sets it on all three', all(els[n]['rect'][3] == 24 for n in ('title', 'body', 'sys')),
      [els[n]['rect'] for n in ('title', 'body', 'sys')])

# A box and a bar: both blend, no Font; picking a blend sets both.
els, out, rects = run(select(BAR, HP) + ['--click', '@ui.blend'] + W + ['--click', '@ui.blend/Add'] + W)
check('a box and a bar: the Blend row, no Font row', 'ui.blend' in rects and 'ui.font' not in rects,
      sorted(k for k in rects if k.startswith('ui.')))
check('a Blend pick sets it on both', els['bar'].get('blend') == els['hp'].get('blend') and els['bar'].get('blend') not in (None, 0, 'opaque'),
      (els['bar'].get('blend'), els['hp'].get('blend')))
check('the Blend then reads Add', shown(out, 'blend') == 'Add', shown(out, 'blend'))
# A text in a font and a box: both blend, Colour, no Font.
els, out, rects = run(select(BAR, TITLE))
check('a font text and a box: Colour and Blend, no Font', 'ui.col' in rects and 'ui.font' not in rects and 'ui.blend' in rects,
      sorted(k for k in rects if k.startswith('ui.')))
# A system-font text and a box: neither Font nor Blend, still Colour.
els, out, rects = run(select(BAR, SYS))
check('a system-font text and a box: Colour, but neither Font nor Blend',
      'ui.col' in rects and 'ui.font' not in rects and 'ui.blend' not in rects, sorted(k for k in rects if k.startswith('ui.')))

print('FAILS=%d' % fails)
sys.exit(1 if fails else 0)
