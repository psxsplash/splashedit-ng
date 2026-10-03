# Lua compiler check: every lua/*.lua compiled with `splashpack-cli luac` must
# match lua/expected/<name>.luac byte for byte. The expected files were written
# by psxsplash's tools/luac_psx running in pcsx-redux.
# usage: python3 luacheck.py <splashpack-cli>
import glob, os, subprocess, sys, tempfile

cli = sys.argv[1]
here = os.path.dirname(os.path.abspath(__file__))
tmp = tempfile.mkdtemp()
fails = 0


def compile_lua(src):
    out = os.path.join(tmp, os.path.basename(src) + 'c')
    r = subprocess.run([cli, 'luac', src, '-o', out], capture_output=True, text=True)
    return (open(out, 'rb').read() if r.returncode == 0 else None), r.stderr.strip()


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + (': ' + str(detail) if detail and not ok else ''))
    fails += not ok


sources = sorted(glob.glob(os.path.join(here, 'lua', '*.lua')))
for src in sources:
    name = os.path.splitext(os.path.basename(src))[0]
    expected = os.path.join(here, 'lua', 'expected', name + '.luac')
    got, err = compile_lua(src)
    if got is None:
        check(name, False, err)
    elif not os.path.exists(expected):
        check(name, False, 'no expected/' + name + '.luac')
    else:
        want = open(expected, 'rb').read()
        diff = next((i for i, (a, b) in enumerate(zip(got, want)) if a != b), min(len(got), len(want)))
        check(name, got == want, f'{len(got)} bytes vs {len(want)} expected, first difference at {diff}')

# The comparison must be able to fail: a changed source gives other bytes.
if sources:
    changed = os.path.join(tmp, 'changed.lua')
    open(changed, 'wb').write(b'local changed = 1\n' + open(sources[0], 'rb').read())
    got, err = compile_lua(changed)
    want = open(os.path.join(here, 'lua', 'expected', os.path.splitext(os.path.basename(sources[0]))[0] + '.luac'), 'rb').read()
    check('a changed source does not match', got is not None and got != want, err)

bad = os.path.join(tmp, 'bad.lua')
open(bad, 'w').write('local x = 010\n')
got, err = compile_lua(bad)
check('syntax error is reported', got is None and 'malformed number' in err, err)

print(f'{len(sources)} files, {fails} failures')
sys.exit(1 if fails or not sources else 0)
