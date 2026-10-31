# The UI tab: a text edit in the element inspector lands on the element it was
# started on (#303). Type into the Text field of one text element, then, with
# the field still focused, click another text element: the typed text belongs
# to the first one, and the second keeps its own. Click empty canvas instead:
# the typed text is kept. Enter commits as before. Opens a copy of
# tests/ui/ui.scene with --ui, then saves and reads the scene back. Needs a
# display (xvfb-run -a).
# usage: python3 uiedittargetcheck.py <splashedit>
import json, os, re, shutil, subprocess, sys, tempfile

editor = os.path.abspath(sys.argv[1])
here = os.path.dirname(os.path.abspath(__file__))
fails = 0
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def project():
    """tests/ui/ui.scene as a project of its own, fonts and texture copied in."""
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
    json.dump(s, open(os.path.join(d, 'ui.scene'), 'w'), indent=1)
    return d


def run(args):
    """Runs the editor on a fresh project with the UI tab open, then Ctrl+S;
    returns the saved canvas and the editor's output."""
    d = project()
    cmd = [editor, '--size', '1600x960', '--project', d, '--ui', '--wait', '5'] + args + [
        '--key', 'ctrl+s', '--wait', '5', '--screenshot', os.path.join(d, 'shot.png')]
    r = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=120)
    out = (r.stdout + r.stderr).strip()
    scene = json.load(open(os.path.join(d, 'ui.scene'), encoding='utf-8'))
    return scene['canvases'][0], out


def elements(cv):
    return {e['name']: e for e in cv['elements']}


# Where the canvas and the element list are: the editor prints it with --ui.
cv0, out = run([])
m = re.search(r'uicanvas: screen ([\d.]+),([\d.]+) scale ([\d.]+) list ([\d.]+),([\d.]+) row ([\d.]+)', out)
check('--ui opens the UI tab and reports its layout', m is not None, out[-300:])
if not m:
    print(f'{fails} failures')
    sys.exit(1)
X, Y, S, LX, LY, LH = map(float, m.groups())
ORDER = [e['name'] for e in cv0['elements']]
BASE = elements(cv0)
A, B = 'title', 'body'  # two text elements, one above the other
check('the scene has the two text elements', BASE.get(A, {}).get('type') == 'text' and BASE.get(B, {}).get('type') == 'text',
      sorted(BASE))


def at(x, y):
    """A click on canvas pixel (x, y), at the pixel's centre."""
    return ['--click', '%d,%d' % (round(X + (x + 0.5) * S), round(Y + (y + 0.5) * S)), '--wait', '3']


def row(name):
    """A click on `name`'s row in the element list."""
    return ['--click', '%d,%d' % (round(LX + 40), round(LY + (ORDER.index(name) + 0.5) * LH)), '--wait', '3']


def onA():
    """'title' (8,8 300x20) by its middle."""
    return at(150, 14)


def onB():
    """'body' (8,32 300x16) by its middle."""
    return at(150, 40)


EMPTY = at(160, 170)  # no element covers the middle of the lower canvas
TYPE = ['--click', '@ui.text', '--wait', '3', '--text', 'AAA', '--wait', '3']


def texts(cv):
    e = elements(cv)
    return e[A].get('text'), e[B].get('text')


# (1) Type into A's Text, then click B on the canvas while the field is focused.
cv, out = run(onA() + TYPE + onB())
a, b = texts(cv)
check('arm 1: clicking B on the canvas keeps the typed text on A', a == 'AAA', (a, b))
check('arm 1: and B keeps its own text', b == BASE[B]['text'], (a, b))
check('arm 1: B ends up selected', 'uicanvas: inspector element ' + B in out, out[-300:])

# (1b) The same through the element list.
cv, out = run(row(A) + TYPE + row(B))
a, b = texts(cv)
check('arm 1b: clicking B in the element list keeps the typed text on A', a == 'AAA', (a, b))
check('arm 1b: and B keeps its own text', b == BASE[B]['text'], (a, b))

# (2) Type into A's Text, then click an empty part of the canvas preview.
cv, out = run(onA() + TYPE + EMPTY)
a, b = texts(cv)
check('arm 2: clicking empty canvas keeps the typed text on A', a == 'AAA', (a, b))
check('arm 2: and B keeps its own text', b == BASE[B]['text'], (a, b))

# (2b) The text kept by (1) is one undo step, like a commit by Enter.
cv, out = run(onA() + TYPE + onB() + ['--key', 'ctrl+z', '--wait', '3'])
a, b = texts(cv)
check('arm 2b: Ctrl+Z takes the text kept on A back', a == BASE[A]['text'] and b == BASE[B]['text'], (a, b))

# (3) Control: Enter commits into A; clicking B afterwards changes nothing.
cv, out = run(onA() + TYPE + ['--key', 'enter', '--wait', '3'] + onB())
a, b = texts(cv)
check('arm 3: Enter commits the typed text into A', a == 'AAA', (a, b))
check('arm 3: and B keeps its own text', b == BASE[B]['text'], (a, b))

print(f'{fails} failures')
sys.exit(1 if fails else 0)
