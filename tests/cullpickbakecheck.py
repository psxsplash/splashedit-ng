# Picking and a Fixed camera's bake keep the Scene view's back-face cull
# (#386 follow-ups). The cullcheck project: Front (red, normals toward the
# camera) at x = -3 and Back (blue, normals away) at x = 3, both 4 x 4 quads
# of the same corners. Pick: a click where Back would be selects nothing
# while culling is on (it is not drawn) and Back with --no-cull; a click on
# Front selects Front either way. Bake: a Fixed camera at (0, 0, -10) looking
# down +Z bakes Front and no Back, as psxsplash would draw them, whatever the
# Scene view's toggle (the bake is made once with --no-cull too).
# usage: python3 cullpickbakecheck.py <splashedit>   (needs a display: xvfb-run -a)
import json, os, re, subprocess, sys

here = os.path.dirname(os.path.abspath(__file__))
sys.argv = sys.argv[:2]  # cullcheck's helpers below read no screenshot argument
editor = os.path.abspath(sys.argv[1])
fails = 0
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')
W = lambda n: ['--wait', str(n)]
BAKE = 'bakes/cull-Cam.bgbake'

# The project and PNG helpers from cullcheck, without running its checks.
src = open(os.path.join(here, 'cullcheck.py')).read()
helpers = {}
exec(compile(src.split('\ndef run(')[0], 'cullcheck.py', 'exec'), helpers)
project, read_png = helpers['project'], helpers['read_png']


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def run(root, args, shot):
    cmd = [editor, '--size', '1600x960', '--project', root, '--no-overlays', '--clean'] + W(10) + args + \
          ['--move', '760,940'] + W(10) + ['--screenshot', shot]
    r = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=300)
    lines = (r.stdout + r.stderr).strip().splitlines()
    missing = [l for l in lines if 'no such control drawn' in l]
    check(os.path.basename(shot) + ': every control the script names was drawn', not missing, missing)
    return lines


def rects(lines):
    out = {}
    for l in lines:
        m = re.match(r'control (\S+(?: \S+)*) at (-?\d+),(-?\d+) (-?\d+),(-?\d+)$', l)
        if m: out[m.group(1)] = tuple(int(v) for v in m.groups()[1:])
    return out


red = lambda p: p[0] > 120 and p[1] < 80 and p[2] < 80
blue = lambda p: p[2] > 120 and p[0] < 80 and p[1] < 80

# 1. Where the quads are: both drawn, culling off.
root = project()
shot = os.path.join(root, 'both.png')
lines = run(root, ['--no-cull'], shot)
v = rects(lines).get('vp.view')
centre = {}
if v and os.path.exists(shot):
    _, _, rows = read_png(shot)
    for name, test in (('Front', red), ('Back', blue)):
        pts = [(x, y) for y in range(v[1] + 60, v[3] - 50, 2) for x in range(v[0] + 4, v[2] - 4, 2)
               if test(rows[y][3 * x:3 * x + 3])]
        if pts: centre[name] = (sum(p[0] for p in pts) // len(pts), sum(p[1] for p in pts) // len(pts))
check('with culling off both quads are on screen', 'Front' in centre and 'Back' in centre, centre)


def click_selection(name, args):
    if name not in centre: return None
    lines = run(root, ['--select', name == 'Front' and 'Back' or 'Front'] + W(5) + args +
                ['--click', '%d,%d' % centre[name]] + W(10), os.path.join(root, 'pick.png'))
    sel = [l for l in lines if l.startswith('selection: ')]
    return sel[-1] if sel else None


s = click_selection('Back', [])
check('culling on: a click where Back would be selects nothing', s == 'selection: 0 all=', s)
s = click_selection('Back', ['--no-cull'])
check('culling off: the same click selects Back', s == 'selection: 1 primary=Back all=Back', s)
s = click_selection('Front', [])
check('culling on: a click on Front selects Front', s == 'selection: 1 primary=Front all=Front', s)

# 2. The bake: a Fixed camera facing Front's side.
scene = os.path.join(root, 'cull.scene')
sc = json.load(open(scene))
sc['objects'].append({'name': 'Cam', 'active': True, 'transform': {'position': [0, 0, -10], 'rotation': [0, 0, 0, 1],
                      'scale': [1, 1, 1]}, 'components': [], 'children': []})
json.dump(sc, open(scene, 'w'), indent=1)
ADD = ['--click', '@obj.addcomp'] + W(3) + ['--text', 'fixed camera'] + W(2) + ['--key', 'enter'] + W(5)
BAKE_ARGS = ['--scroll-to', '@fcbake', '--click', '@fcbake'] + W(10) + ['--wait-export'] + W(5)
for arm, extra in (('cull on', []), ('cull off', ['--no-cull'])):
    bake = os.path.join(root, BAKE)
    if os.path.exists(bake): os.remove(bake)
    run(root, extra + ['--select', 'Cam'] + W(10) + ADD + BAKE_ARGS + ['--key', 'ctrl+s'] + W(10), os.path.join(root, 'bake.png'))
    meta = json.load(open(bake)) if os.path.exists(bake) else {}
    img = os.path.join(os.path.dirname(bake), meta.get('image', 'missing.png'))
    r = b = n = 0
    if os.path.exists(img):
        w, h, rows = read_png(img)
        for y in range(h):
            for x in range(w):
                p = rows[y][3 * x:3 * x + 3]
                n += 1
                r += red(p)
                b += blue(p)
    check(arm + ': the bake was written', n == 320 * 240, (meta, n))
    check(arm + ': the bake shows Front', r > 0.02 * max(n, 1), r)
    check(arm + ': the bake leaves out Back, as the console culls it', n > 0 and b == 0, b)
    # Later runs add the component again: start from the scene without it.
    json.dump(sc, open(scene, 'w'), indent=1)

print('cullpickbakecheck: %s' % ('all passed' if not fails else '%d failed' % fails))
sys.exit(1 if fails else 0)
