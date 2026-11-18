# The scene inspector's Sprite sheets > Texture takes an image dragged from the
# Project panel, as a UI image's Texture does (#160): the sheet's texture
# becomes the dropped image, and a file that is no image is refused there and
# changes nothing. Drives the editor headless on copies of the courtyard
# example with a sheet "red" on textures/sred.png and a second 32x32 PNG,
# textures/sblue.png; --scroll-to brings the Texture row (scene.spritetex)
# into the inspector, a first run reads where it is drawn and the next drags
# the Project panel's first file onto it, then Ctrl+S, and reads the sheet's
# texture from the saved scene.
# usage: python3 spritesheetdropcheck.py <splashedit> [screenshot.png]   (needs a display: xvfb-run -a)
import json, os, re, shutil, struct, subprocess, sys, tempfile, zlib

editor = os.path.abspath(sys.argv[1])
shotOut = sys.argv[2] if len(sys.argv) > 2 else None
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
fails = checks = 0
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')
H = 960
SEARCH = '880,731'  # the Project panel's search field at 1600x960 (as assetbrowsercheck.py)
TILE = '530,812'    # the first file the search shows
FIELD = 'scene.spritetex'


def check(name, ok, detail=''):
    global fails, checks
    checks += 1
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def png(path, rgb):
    """A 32x32 opaque PNG of one colour."""
    raw = b''.join(b'\0' + bytes(rgb + (255,)) * 32 for _ in range(32))
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 32, 32, 8, 6, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def project():
    """A courtyard copy with textures/sred.png and textures/sblue.png and one
    sprite sheet, "red", cut from sred.png into 16x16 cells."""
    d = os.path.join(tempfile.mkdtemp(), 'courtyard')
    shutil.copytree(example, d)
    png(os.path.join(d, 'textures', 'sred.png'), (200, 30, 30))
    png(os.path.join(d, 'textures', 'sblue.png'), (30, 30, 200))
    scene = os.path.join(d, 'courtyard.scene')
    j = json.load(open(scene, encoding='utf-8'))
    j['spriteSheets'] = [{'name': 'red', 'texture': 'textures/sred.png', 'bitDepth': 4, 'cellWidth': 16, 'cellHeight': 16,
                          'anims': []}]
    json.dump(j, open(scene, 'w', encoding='utf-8'), indent=2)
    return d, scene


def run(search=None, drop=None, shot=None):
    """Scrolls the inspector to the sheet's Texture; with `search` and `drop`
    (x, y), searches the Project panel and drags its first file there, then
    saves. Returns the saved sheet's texture, the output and the rects."""
    d, scene = project()
    cmd = [editor, '--size', '1600x%d' % H, '--project', d, '--scene', scene, '--wait', '20', '--wait-export',
           '--scroll-to', '@' + FIELD, '--wait', '3']
    if search:
        cmd += ['--click', SEARCH + ',0', '--text', search, '--wait', '5', '--drag', '%s,%d,%d,0' % (TILE, drop[0], drop[1]),
                '--wait', '10']
    cmd += ['--key', 'ctrl+s', '--wait', '10', '--screenshot', shot or os.path.join(d, 'shot.png')]
    r = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=240)
    lines = (r.stdout + r.stderr).strip().splitlines()
    rects = {}
    for l in lines:
        m = re.match(r'control (\S+) at (-?\d+),(-?\d+) (-?\d+),(-?\d+)$', l)
        if m: rects[m.group(1)] = tuple(int(v) for v in m.groups()[1:])
    out = '\n'.join(l for l in lines if not l.startswith('control '))
    sheets = json.load(open(scene, encoding='utf-8')).get('spriteSheets') or [{}]
    return sheets[0].get('texture'), out, rects


def centre(r):
    return ((r[0] + r[2]) // 2, (r[1] + r[3]) // 2)


tex, out, rects = run()
at = rects.get(FIELD)
check('the sheet starts on sred.png', tex == 'textures/sred.png', (tex, out[-300:]))
check(FIELD + ' is drawn inside the inspector', bool(at) and at[1] >= 80 and at[3] <= H - 40, (at, out[-300:]))
if at:
    tex, out, rects = run('sblue', centre(at), shot=shotOut)
    check('an image dropped on Texture becomes the sheet\'s texture', tex == 'textures/sblue.png', (tex, out[-300:]))
    check('the section reports the new texture', 'scene: sprite 0 red texture textures/sblue.png' in out, out[-400:])
    tex, out, rects = run('courtyard.scene', centre(at))
    check('a scene dropped on Texture is refused and leaves it', tex == 'textures/sred.png', (tex, out[-300:]))
    check('the status bar says so', 'That field does not take' in out, out[-300:])

print(f'{checks - fails}/{checks} checks passed')
print(f'{fails} failures')
sys.exit(1 if fails else 0)
