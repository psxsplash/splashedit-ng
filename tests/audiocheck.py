# Encode tests/audio/*.wav with `splashpack-cli audio` at their own rate and
# compare with tests/audio/expected/, which psxavenc (-t spu -f <rate> [-L])
# produced from the same 16-bit mono PCM. Byte-identical is the pass.
# usage: python3 audiocheck.py <splashpack-cli>
import glob, os, re, subprocess, sys, tempfile, wave

cli = sys.argv[1]
here = os.path.dirname(os.path.abspath(__file__))
tmp = tempfile.mkdtemp()
fails = checks = 0


def encode(wav, rate, loop, out):
    args = [cli, 'audio', wav, '--rate', str(rate), '-o', out] + (['--loop'] if loop else [])
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stderr.strip())
    return r.stdout


def check(name, ok, detail=''):
    global fails, checks
    checks += 1
    fails += not ok
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + detail))


wavs = sorted(glob.glob(os.path.join(here, 'audio', '*.wav')))
for wav in wavs:
    base = os.path.splitext(os.path.basename(wav))[0]
    rate = wave.open(wav).getframerate()
    for loop in (0, 1):
        out = os.path.join(tmp, f'{base}-{loop}.bin')
        encode(wav, rate, loop, out)
        exp = open(os.path.join(here, 'audio', 'expected', f'{base}-loop{loop}.spu'), 'rb').read()
        check(f'{base} loop={loop}', open(out, 'rb').read() == exp, 'differs from psxavenc')

# The comparison must be able to fail: looped output against the one-shot file.
if wavs:
    base = os.path.splitext(os.path.basename(wavs[0]))[0]
    rate = wave.open(wavs[0]).getframerate()
    out = os.path.join(tmp, 'neg.bin')
    encode(wavs[0], rate, 1, out)
    exp = open(os.path.join(here, 'audio', 'expected', f'{base}-loop0.spu'), 'rb').read()
    check('mismatch is detected', open(out, 'rb').read() != exp)
    # Resampled to half rate, the ADPCM must still decode close to the PCM it
    # was encoded from.
    snr = float(re.search(r'snr=([-0-9.]+)', encode(wavs[0], rate // 2, 0, out)).group(1))
    check(f'half-rate SNR {snr:.1f} dB above 15', snr > 15, str(snr))

print(f'{len(wavs)} files, {checks} checks, {fails} failures')
sys.exit(1 if fails or not wavs else 0)
