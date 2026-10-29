# The Disc section's licence builder, driven through the GUI on a copy of the
# courtyard: Licence > Build... opens rows under the licence (Logo, Top line,
# Bottom line, a status line, Build and Cancel), prefilled with the psxsplash
# licence's text; the lines are replaced with HELLO and WORLD and Build writes
# <project>/custom-licence.dat (12 x 2336 bytes, the lines space-padded at the
# start of sector 4's data) and makes it the licence in disc.json. Cancel
# closes the rows and writes nothing. While open the editor prints
# 'disc: licence build "<top>" "<bottom>" <status>' whenever it changes.
# usage: python3 licencebuildcheck.py <splashedit> [<builder.png>]   (needs a display: xvfb-run -a)
import json, os, shutil, subprocess, sys, tempfile

editor = os.path.abspath(sys.argv[1])
shot_out = sys.argv[2] if len(sys.argv) > 2 else None
here = os.path.dirname(os.path.abspath(__file__))
example = os.path.join(here, '..', 'examples', 'courtyard')
fails = 0
ENV = dict(os.environ, SPLASHEDIT_REDUX='/nonexistent/pcsx-redux', SPLASHEDIT_PSXSPLASH='/nonexistent/psxsplash.ps-exe')
# Folding the sections above it brings the Disc section (open from the
# start) into view at 1600x960, as discsettingscheck.py does.
FOLDS = ['--click', '@s_sequences', '--wait', '3', '--click', '@s_rendering', '--wait', '3', '--click', '@s_loading', '--wait', '3',
         '--click', '@s_vram', '--wait', '5']
BUILDER = ('disc.licence.tmd', 'disc.licence.top', 'disc.licence.bottom', 'disc.licence.build', 'disc.licence.cancel')


def check(name, ok, detail=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))
    fails += not ok


def run(args):
    """Runs the editor on a fresh courtyard copy, folds the sections above
    Disc, then `args`. Returns the project, the printed disc: lines, all
    output lines and the named controls of the last frame."""
    project = os.path.join(tempfile.mkdtemp(), 'courtyard')
    shutil.copytree(example, project)
    cmd = [editor, '--size', '1600x960', '--project', project, '--wait', '5'] + FOLDS + args + \
          ['--wait', '5', '--screenshot', os.path.join(project, 'end.png')]
    r = subprocess.run(cmd, capture_output=True, text=True, env=ENV, timeout=180)
    lines = (r.stdout + r.stderr).strip().splitlines()
    missing = [l for l in lines if 'no such control drawn' in l]
    check('every control the script names was drawn', not missing, missing)
    controls = set()
    for l in lines:
        if l.startswith('control ') and ' at ' in l:
            controls.add(l[len('control '):].split(' at ')[0])
    return project, [l for l in lines if l.startswith('disc:')], lines, controls


def click(name, wait='3'):
    return ['--click', '@' + name, '--wait', wait]


# Licence > Build..., then the inspector scrolled until the Cancel button
# (the rows' last) is inside it.
OPEN = ['--click', '@disc.licence', '--wait', '3', '--click', '@disc.licence/Build...', '--wait', '5',
        '--scroll-to', '@disc.licence.cancel', '--wait', '3']


def disc_json(project):
    p = os.path.join(project, 'disc.json')
    return json.load(open(p)) if os.path.exists(p) else None


# Closed by default: none of the builder's controls are drawn.
project, discs, lines, controls = run([])
check('the builder rows are closed by default', not any(n in controls for n in BUILDER),
      sorted(n for n in controls if n.startswith('disc.licence')))
check('no builder line while closed', not any(l.startswith('disc: licence build') for l in discs), discs)

# Build...: the rows show the psxsplash text and its logo's size; HELLO and
# WORLD replace the lines and Build writes and picks custom-licence.dat.
shot = os.path.join(tempfile.mkdtemp(), 'builder.png')
project, discs, lines, controls = run(
    OPEN +
    click('disc.licence.top') + ['--text', 'HELLO', '--wait', '3'] +
    click('disc.licence.bottom') + ['--text', 'WORLD', '--wait', '3', '--shot', shot] +
    click('disc.licence.build', '5'))
builds = [l for l in discs if l.startswith('disc: licence build ')]
check('the builder opens prefilled with the psxsplash text and logo',
      bool(builds) and builds[0] == 'disc: licence build "     Created wit        h" "                psxsplash" '
                                    'TMD ok, 601 triangles', builds[:1])
check('the typed lines reach the builder', 'disc: licence build "HELLO" "WORLD" TMD ok, 601 triangles' in builds, builds)
data = disc_json(project)
check('Build makes custom-licence.dat the licence in disc.json', (data or {}).get('licence') == 'custom-licence.dat', data)
check('the licence change is printed', 'disc: licence custom-licence.dat' in discs, discs)
check('no licence error after the build', not any(l.startswith('disc: licence error') for l in discs), discs)
lic_path = os.path.join(project, 'custom-licence.dat')
lic = open(lic_path, 'rb').read() if os.path.exists(lic_path) else b''
check('custom-licence.dat is 28032 bytes', len(lic) == 28032, len(lic))
s4 = lic[4 * 2336 + 8:4 * 2336 + 8 + 64]
check('sector 4 starts with HELLO and WORLD space-padded', s4 == b'HELLO'.ljust(32) + b'WORLD'.ljust(32), s4)
check('Build closes the rows', not any(n in controls for n in BUILDER),
      sorted(n for n in controls if n.startswith('disc.licence')))
check('the builder screenshot was taken', os.path.exists(shot), shot)
if shot_out and os.path.exists(shot):
    shutil.copyfile(shot, shot_out)

# Cancel closes the rows and writes nothing.
project, discs, lines, controls = run(OPEN +
                                      click('disc.licence.cancel', '5'))
check('Cancel closes the rows', not any(n in controls for n in BUILDER),
      sorted(n for n in controls if n.startswith('disc.licence')))
check('Cancel writes nothing', disc_json(project) is None and not os.path.exists(os.path.join(project, 'custom-licence.dat')),
      disc_json(project))

# A line past 32 bytes cannot be typed (the field stops at 32), and a byte
# outside printable ASCII is named in the status line and blocks Build.
project, discs, lines, controls = run(OPEN +
                                      click('disc.licence.top') + ['--text', 'café', '--key', 'enter', '--wait', '3'] +
                                      click('disc.licence.build', '5'))
builds = [l for l in discs if l.startswith('disc: licence build ')]
check('a non-ASCII line is named in the status', any('Top line: byte 4 is 0xc3' in l for l in builds), builds)
check('Build is refused while the status is an error', disc_json(project) is None and
      not os.path.exists(os.path.join(project, 'custom-licence.dat')), disc_json(project))
check('the rows stay open on a refused Build', all(n in controls for n in BUILDER), sorted(controls & set(BUILDER)))

print('%d failure%s' % (fails, '' if fails == 1 else 's'))
sys.exit(1 if fails else 0)
