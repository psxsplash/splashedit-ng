# Build > Tools... opens the tools popup with every path Play, Export and Run
# on hardware use (pcsx-redux, the psxsplash build, BIOS, the disc build),
# each with its Locate button, even when nothing is missing. Before it the
# popup only opened on Play, Export or Run with a tool missing, so a path set
# once could only be changed by editing play.cfg.
# usage: python3 toolscheck.py <splashedit>   (needs a display: xvfb-run -a)
import os, shutil, subprocess, sys, tempfile

editor = os.path.abspath(sys.argv[1])
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
fails = 0
work = tempfile.mkdtemp()
engines = os.path.join(work, 'engine')
os.makedirs(engines)
for name in ('psxsplash.ps-exe', 'psxsplash-cdrom.ps-exe', 'pcsx-redux'):
    with open(os.path.join(engines, name), 'wb') as fh:
        fh.write(b'PS-X EXE fake')
settings = os.path.join(work, 'prefs', 'play.cfg')
os.makedirs(os.path.dirname(settings))
with open(settings, 'w') as fh:
    fh.write('redux=%s\npsxsplash=%s\ndisc=%s\n' % (os.path.join(engines, 'pcsx-redux'),
                                                     os.path.join(engines, 'psxsplash.ps-exe'),
                                                     os.path.join(engines, 'psxsplash-cdrom.ps-exe')))


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


project = os.path.join(tempfile.mkdtemp(), 'courtyard')
shutil.copytree(example, project)
cmd = [editor, '--size', '1600x960', '--project', project, '--play-settings', settings, '--wait', '5',
       '--click', '@menu.build', '--wait', '3', '--click', '@menu.build/Tools...', '--wait', '5',
       '--screenshot', os.path.join(project, 'shot.png')]
r = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
lines = (r.stdout + r.stderr).splitlines()
missing = [l for l in lines if 'no such control drawn' in l]
check('Build > Tools... is in the menu', not missing, missing)
drawn = {l.split()[1] for l in lines if l.startswith('control ')}
for row in ('locate.redux', 'locate.psxsplash', 'locate.bios', 'locate.disc'):
    check('the popup has ' + row + ' with every tool set', row in drawn, sorted(d for d in drawn if d.startswith('locate')))
check('it closes with Done, not Play', 'go' in drawn, sorted(drawn)[:40])
print('%d failures' % fails)
sys.exit(1 if fails else 0)
