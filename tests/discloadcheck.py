# The disc export's load estimate (issue #214), driven through the GUI on a
# copy of the courtyard with a two-scene build list (courtyard, then yard:
# the courtyard with a loading screen, so its splashpack is read in 32-sector
# chunks): Export (Ctrl+B) writes the disc, and the status bar's
# notice ends "Load: courtyard ~X s, yard ~Y s". Hovering the notice shows
# each scene's reads and sectors and says the figure is estimated from the
# emulator's CD timing. The editor prints "disc load: scene <i> <name> ~X s
# reads R sectors S" per scene; those are checked against the files on the
# written disc, priced as psxsplash reads them (disccheck.py's rule). The
# engine is a fake CD-ROM build (SPLASHEDIT_DISC_ENGINE), as bootlogocheck.py.
# usage: python3 discloadcheck.py <splashedit> [<shot.png>]   (needs a display: xvfb-run -a)
import json, os, re, shutil, struct, subprocess, sys, tempfile

editor = os.path.abspath(sys.argv[1])
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
fails = 0
work = tempfile.mkdtemp()
shot = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else os.path.join(work, 'shot.png')
plain = shot[:-len('.png')] + '-notice.png'  # before the hover: the notice line itself


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def iso_dirs(path):
    """'DIR' -> {name: size} for each scene directory on a raw Mode 2 Form 1 image."""
    raw = open(path, 'rb').read()
    sec = [raw[i * 2352 + 24:i * 2352 + 24 + 2048] for i in range(len(raw) // 2352)]

    def entries(lba, size):
        data = b''.join(sec[lba:lba + (size + 2047) // 2048])[:size]
        i, out = 0, {}
        while i < len(data):
            n = data[i]
            if n == 0:
                i = (i // 2048 + 1) * 2048
                continue
            name = data[i + 33:i + 33 + data[i + 32]]
            if name not in (b'\0', b'\1'):
                out[name.decode()] = (struct.unpack_from('<I', data, i + 2)[0], struct.unpack_from('<I', data, i + 10)[0],
                                      bool(data[i + 25] & 2))
            i += n
        return out

    root = sec[16][156:156 + 34]
    top = entries(struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0])
    return {k: {n: v[1] for n, v in entries(v[0], v[1]).items()} for k, v in top.items() if v[2]}


def expected(files, i):
    """(reads, sectors, ms) for scene i: 2 directory reads, LDG, VRM, SPU one read each, the SPK in
    32-sector chunks behind a loading screen (else one read); PCSX-Redux CD timing at 2x."""
    size = lambda ext: files.get('SCENE_%d.%s;1' % (i, ext), 0)
    sect = lambda b: (b + 2047) // 2048
    reads = sectors = 2
    for ext in ('LDG', 'VRM', 'SPU'):
        if size(ext):
            reads, sectors = reads + 1, sectors + sect(size(ext))
    spk = sect(size('SPK'))
    reads += (spk + 31) // 32 if size('LDG') else 1
    return reads, sectors + spk, (reads * 66.5 + (sectors + spk - reads) * 1000 / 150)


project = os.path.join(work, 'courtyard')
shutil.copytree(example, project)
# yard has a loading screen (disccheck.py's), so psxsplash reads its splashpack in 32-sector chunks.
doc = json.load(open(os.path.join(project, 'courtyard.scene'), encoding='utf-8'))
doc.setdefault('canvases', []).append({'name': 'Loading', 'elements': [
    {'type': 'box', 'name': 'bg', 'rect': [0, 0, 320, 240], 'color': [0.1, 0.2, 0.5]},
    {'type': 'progress', 'name': 'loading', 'rect': [40, 200, 240, 8], 'color': [1, 1, 1],
     'background': [0.2, 0.2, 0.2], 'value': 0}]})
doc['settings']['loadingCanvas'] = 'Loading'
json.dump(doc, open(os.path.join(project, 'yard.scene'), 'w', encoding='utf-8'))
with open(os.path.join(project, 'scenes.list'), 'w') as fh:
    fh.write('courtyard.scene\nyard.scene\n')
eng = os.path.join(work, 'psxsplash-cdrom.ps-exe')
with open(eng, 'wb') as fh:  # a stand-in CD-ROM psxsplash (disccheck.py's)
    fh.write(b'PS-X EXE' + bytes(2040) + b'CD' * 300 + b'SCENE%d/SCENE_%d.SPK;1\0' + bytes(100))
env = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe',
           SPLASHEDIT_DISC_ENGINE=eng)
out = os.path.join(work, 'game.bin')
cmd = [editor, '--size', '1600x960', '--project', project, '--play-settings', os.path.join(work, 'play.cfg'),
       '--export-to', out, '--wait', '5', '--key', 'ctrl+b', '--wait-play', '--wait', '5',
       '--shot', plain, '--move', '@notice', '--wait', '90', '--screenshot', shot]
r = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=240)
lines = (r.stdout + r.stderr).strip().splitlines()
missing = [l for l in lines if 'no such control drawn' in l]
check('every control the script names was drawn (the notice included)', not missing, missing)
check('the disc was written', os.path.exists(out), [l for l in lines if 'build report' in l])

loads = [l for l in lines if l.startswith('disc load: ')]
check('one estimate per scene, in build list order',
      [l.split()[4] for l in loads] == ['courtyard', 'yard'] and [l.split()[3] for l in loads] == ['0', '1'], loads)
if os.path.exists(out):
    dirs = iso_dirs(out)
    yard = dirs.get('SCENE1', {})
    check('yard has a loading screen and a splashpack over one 32-sector chunk (chunked reads are exercised)',
          yard.get('SCENE_1.LDG;1', 0) > 0 and yard.get('SCENE_1.SPK;1', 0) > 32 * 2048, yard)
    for i, l in enumerate(loads):
        reads, sectors, ms = expected(dirs.get('SCENE%d' % i, {}), i)
        want = 'disc load: scene %d %s ~%.1f s reads %d sectors %d' % (i, l.split()[4], ms / 1000, reads, sectors)
        check('scene %d: reads, sectors and time as the files on the disc' % i, l == want, (l, want))
check('hovering the notice shows its estimate tip', 'notice tip: shown' in lines,
      [l for l in lines if l.startswith('notice')])
check('both screenshots were taken', os.path.exists(shot) and os.path.exists(plain), (shot, plain))
print('screenshots:', plain, shot)
print('FAILS=%d' % fails)
sys.exit(1 if fails else 0)
