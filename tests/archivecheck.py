# Build an archive disc (disc --archive) of examples/courtyard twice, the
# second copy with streamWorld on, and one CD-DA track, then read it back the
# way psxsplash's archive loader and a PS1 BIOS do: the root directory at LBA
# 22 holds SYSTEM.CNF and PSX.EXE; the psyqo-paths index ("PSX-ARC1") at LBA
# 23 has its entries sorted by 64-bit DJB2 hash, one per file `build` exports,
# each at an absolute LBA, laid out in load order (loading, vram, spu,
# splashpack, geo), and each one LZ4 (block, padded at the front to end on a
# sector) or stored decoding to the exported bytes; .geo is stored; the
# header's margin covers LZ4's in-place margin; 9000 zero sectors close the
# data track. A movie (.STR) is a STREAM entry whose sectors keep their
# subheaders and a .sbs a plain entry. --archive refuses a CD-ROM engine, and a
# disc for an archive engine is an archive disc without it.
# usage: python3 archivecheck.py <splashpack-cli>
import json, os, shutil, struct, subprocess, sys, tempfile

cli = sys.argv[1]
here = os.path.dirname(os.path.abspath(__file__))
tmp = tempfile.mkdtemp()
fails = checks = 0


def check(name, ok, detail=''):
    global fails, checks
    checks += 1
    fails += not ok
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ': ' + str(detail)))


def done():
    print('%d checks, %d failed' % (checks, fails))
    shutil.rmtree(tmp, ignore_errors=True)
    sys.exit(1 if fails else 0)


# Stand-in engines: a PS-X EXE header and the strings each loader carries.
def engine(path, marker):
    body = b'PS-X EXE' + bytes(2040) + b'code' * 300 + marker + b'\0' + bytes(100)
    open(path, 'wb').write(body)
    return body


arc_exe = engine(os.path.join(tmp, 'arc.ps-exe'), b'scene_%d.splashpack\0FileLoader: archive of %u files via %s')
engine(os.path.join(tmp, 'cd.ps-exe'), b'SCENE%d/SCENE_%d.SPK;1')

proj = os.path.join(tmp, 'proj')
shutil.copytree(os.path.join(here, '..', 'examples', 'courtyard'), proj)
s0 = os.path.join(proj, 'courtyard.scene')
s1 = os.path.join(proj, 'streamed.scene')
j = json.load(open(s0))
j['settings']['streamWorld'] = True
json.dump(j, open(s1, 'w'), indent=2)
tone = os.path.join(here, 'audio', 'formats', 'tone.wav')

# A movie as psxavenc writes it (2336 bytes a sector, subheader twice): seven
# video sectors then one XA audio sector, and a movie texture (.sbs).
SECTORS = 16
VIDEO_SUB = bytes([1, 1, 0x48, 0]) * 2
AUDIO_SUB = bytes([1, 1, 0x64, 0x04]) * 2


def movie_sector(i):
    if i % 8 == 7:
        return AUDIO_SUB + bytes([0x80 + i]) * 2324 + bytes(4)
    head = struct.pack('<HHHHIIHHHHHH', 0x0160, 0x8001, i, SECTORS, 1, 2016, 320, 240, 0x40, 0x3800, 2, 2)
    return VIDEO_SUB + head + bytes(32 - len(head)) + bytes([i & 0xff]) * 2016 + bytes(280)


movie = os.path.join(tmp, 'intro.str')
open(movie, 'wb').write(b''.join(movie_sector(i) for i in range(SECTORS)))
sbs = b''.join(struct.pack('<HHHH', 0x20, 0x3800, 2, 2) + bytes([f]) * 1016 for f in range(4))
sbs_path = os.path.join(tmp, 'intro.sbs')
open(sbs_path, 'wb').write(sbs)

binpath = os.path.join(tmp, 'out', 'psxsplash.bin')
r = subprocess.run([cli, 'disc', s0, s1, '--engine', os.path.join(tmp, 'arc.ps-exe'), '--archive', '-o', binpath,
                    '--track', tone, '--movie', movie, '--movie', sbs_path], capture_output=True, text=True)
check('disc --archive exits 0', r.returncode == 0, r.stderr)
r2 = subprocess.run([cli, 'disc', s0, '--engine', os.path.join(tmp, 'cd.ps-exe'), '--archive', '-o',
                     os.path.join(tmp, 'cd.bin')], capture_output=True, text=True)
check('--archive refuses a CD-ROM engine', r2.returncode != 0 and 'LOADER=archive' in r2.stderr and
      not os.path.exists(os.path.join(tmp, 'cd.bin')), (r2.returncode, r2.stderr))
r3 = subprocess.run([cli, 'disc', s0, '--engine', os.path.join(tmp, 'arc.ps-exe'), '-o',
                     os.path.join(tmp, 'iso.bin')], capture_output=True, text=True)
iso = open(os.path.join(tmp, 'iso.bin'), 'rb').read() if r3.returncode == 0 else b''
check('a disc for an archive engine is an archive disc without --archive',
      r3.returncode == 0 and iso[23 * 2352 + 24:23 * 2352 + 32] == b'PSX-ARC1', (r3.returncode, r3.stderr))
ref = os.path.join(tmp, 'ref')
r4 = subprocess.run([cli, 'build', s0, s1, '-o', ref], capture_output=True, text=True)
check('reference build', r4.returncode == 0, r4.stderr)
if r.returncode or r4.returncode:
    done()

raw = open(binpath, 'rb').read()
check('whole raw sectors', len(raw) % 2352 == 0, len(raw))


def sector(i):
    return raw[i * 2352 + 24:i * 2352 + 24 + 2048]


def submode(i):
    return raw[i * 2352 + 18]


def records(lba, size):
    out = {}
    d = b''.join(sector(lba + k) for k in range((size + 2047) // 2048))
    at = 0
    while at < len(d):
        n = d[at]
        if n == 0:
            at = (at // 2048 + 1) * 2048
            continue
        ident = d[at + 33:at + 33 + d[at + 32]]
        out[ident] = struct.unpack_from('<I', d, at + 2)[0], struct.unpack_from('<I', d, at + 10)[0]
        at += n
    return out


pvd = sector(16)
check('PVD at 16', pvd[:6] == b'\x01CD001', pvd[:6])
check('terminator at 17', sector(17)[:6] == b'\xffCD001')
lt, lopt, mt, mopt = struct.unpack_from('<II', pvd, 140) + struct.unpack_from('>II', pvd, 148)
check('path tables at 18-21', (lt, lopt, mt, mopt) == (18, 19, 20, 21), (lt, lopt, mt, mopt))
root_lba, root_size = struct.unpack_from('<I', pvd, 158)[0], struct.unpack_from('<I', pvd, 166)[0]
check('root directory at 22', root_lba == 22, root_lba)
check('path tables name the root', all(struct.unpack_from('<I', sector(k), 2)[0] == 22 for k in (18, 19)) and
      all(struct.unpack_from('>I', sector(k), 2)[0] == 22 for k in (20, 21)))
root = records(root_lba, root_size)
names = sorted(k for k in root if k not in (b'\0', b'\1'))
check('root lists SYSTEM.CNF and PSX.EXE', names == [b'PSX.EXE;1', b'SYSTEM.CNF;1'], names)
cnf = root.get(b'SYSTEM.CNF;1', (0, 0))
check('SYSTEM.CNF boots PSX.EXE', sector(cnf[0])[:cnf[1]].startswith(b'BOOT = cdrom:\\PSX.EXE;1'))
exe_lba, exe_size = root.get(b'PSX.EXE;1', (0, 0))
exe = b''.join(sector(exe_lba + k) for k in range((exe_size + 2047) // 2048))[:exe_size]
check('PSX.EXE is the engine', exe == arc_exe)

volume = struct.unpack_from('<I', pvd, 80)[0]
exe_end = exe_lba + (exe_size + 2047) // 2048
check('9000 zero sectors close the data track', volume - exe_end == 9000 and
      all(sector(k) == bytes(2048) for k in (exe_end, exe_end + 4500, volume - 1)), (volume, exe_end))
cue = open(binpath[:-4] + '.cue').read()
check('cue has the data track and TRACK 02 AUDIO', 'TRACK 01 MODE2/2352' in cue and 'TRACK 02 AUDIO' in cue, cue)
check('the audio starts after the data track', len(raw) // 2352 > volume + 150)


def djb2(s):
    h = 5381
    for c in s.encode():
        h = ((h * 33) ^ c) & 0xFFFFFFFFFFFFFFFF
    return h


# LZ4 block format, written from the spec (lz4_Block_format.md).
def lz4_block(src):
    out = bytearray()
    i = 0
    while i < len(src):
        tok = src[i]
        i += 1
        n = tok >> 4
        if n == 15:
            while True:
                b = src[i]
                i += 1
                n += b
                if b != 255:
                    break
        out += src[i:i + n]
        i += n
        if i >= len(src):
            break
        off = src[i] | src[i + 1] << 8
        i += 2
        m = tok & 15
        if m == 15:
            while True:
                b = src[i]
                i += 1
                m += b
                if b != 255:
                    break
        m += 4
        start = len(out) - off
        if off >= m:
            out += out[start:start + m]
        else:
            for k in range(m):
                out.append(out[start + k])
    return bytes(out)


try:
    import lz4.block as lz4block
except ImportError:
    lz4block = None

idx = sector(23)
check('archive magic at 23', idx[:8] == b'PSX-ARC1', idx[:8])
word, total = struct.unpack_from('<II', idx, 8)
count, margin_byte = word & 0xFFFFFF, word >> 24
check('archive size is the data track', total == volume, (total, volume))
index_sectors = ((count + 1) * 16 + 2047) // 2048
idx = b''.join(sector(23 + k) for k in range(index_sectors))
entries = []
for k in range(count):
    h, a, b = struct.unpack_from('<QII', idx, 16 * (k + 1))
    entries.append({'hash': h, 'size': a & 0x1FFFFF, 'pad': a >> 21, 'lba': b & 0x7FFFF,
                    'sectors': (b >> 19) & 0x3FF, 'method': b >> 29})
hashes = [e['hash'] for e in entries]
check('index sorted by hash', hashes == sorted(hashes) and len(set(hashes)) == len(hashes))

order = ['loading', 'vram', 'spu', 'splashpack', 'geo']
want = [f'scene_{i}.{k}' for i in range(2) for k in order if os.path.exists(os.path.join(ref, f'scene_{i}.{k}'))]
check('a .geo is in the build', 'scene_1.geo' in want, want)
by_hash = {e['hash']: e for e in entries}
check('one entry per exported file and movie', count == len(want) + 2 and all(djb2(n) in by_hash for n in want),
      (count, want))
lbas = [by_hash[djb2(n)]['lba'] for n in want if djb2(n) in by_hash]
check('entries in load order from the index on, before PSX.EXE', lbas == sorted(lbas) and
      lbas[:1] == [23 + index_sectors] and all(e['lba'] + e['sectors'] <= cnf[0] for e in entries), lbas)
lz4_seen = 0
need_margin = 0
for n in want:
    e = by_hash.get(djb2(n))
    if not e:
        continue
    data = open(os.path.join(ref, n), 'rb').read()
    plain = max(1, (len(data) + 2047) // 2048)
    body = b''.join(sector(e['lba'] + k) for k in range(e['sectors']))
    check(n + ' last sector ends the record', submode(e['lba'] + e['sectors'] - 1) == 0x89)
    check(n + ' size', e['size'] == len(data), (e['size'], len(data)))
    if e['method'] == 0:
        check(n + ' stored = export', e['sectors'] == plain and body[:len(data)] == data and e['pad'] == 0)
    elif e['method'] == 2:
        lz4_seen += 1
        comp = body[e['pad']:]
        check(n + ' LZ4 saves a sector', e['sectors'] < plain, (e['sectors'], plain))
        check(n + ' padding is before the payload', body[:e['pad']] == bytes(e['pad']))
        check(n + ' LZ4 = export', lz4_block(comp) == data)
        if lz4block:
            check(n + ' lz4.block agrees', lz4block.decompress(comp, uncompressed_size=len(data)) == data)
        need_margin = max(need_margin, (len(comp) >> 8) + 32)
    else:
        check(n + ' method stored or LZ4', False, e['method'])
    if n.endswith('.geo'):
        check(n + ' stored, so it can stream', e['method'] == 0, e['method'])
check('some entry is LZ4', lz4_seen > 0)

# The movie: a STREAM entry, its first word the sector count, its sectors as
# psxavenc wrote them (psyqo-paths resolveMovie, psxsplash movieplayer.cpp).
m = by_hash.get(djb2('MOVIES/INTRO.STR'))
check('MOVIES/INTRO.STR is in the index', m is not None)
if m:
    words = struct.unpack_from('<II', idx, 16 * (entries.index(m) + 1) + 8)
    check('INTRO.STR is a STREAM entry (method 3)', m['method'] == 3, m['method'])
    check('its first word is the sector count', words[0] == SECTORS, words[0])
    check('its sectors lie before SYSTEM.CNF', 23 + index_sectors <= m['lba'] and m['lba'] + SECTORS <= cnf[0],
          (m['lba'], cnf[0]))
    check('every movie sector keeps its subheader',
          all(raw[(m['lba'] + i) * 2352 + 16:(m['lba'] + i) * 2352 + 24] == (AUDIO_SUB if i % 8 == 7 else VIDEO_SUB)
              for i in range(SECTORS)))
    au = (m['lba'] + 7) * 2352
    check('audio stays Form 2, all 2324 bytes', raw[au + 24:au + 24 + 2324] == bytes([0x87]) * 2324)
    check('video sectors hold their chunks',
          all(raw[(m['lba'] + i) * 2352 + 24:(m['lba'] + i) * 2352 + 26] == b'\x60\x01' for i in range(SECTORS)
              if i % 8 != 7))
t = by_hash.get(djb2('MOVIES/INTRO.SBS'))
check('MOVIES/INTRO.SBS is a plain entry', t is not None and t['method'] in (0, 2))
if t:
    body = b''.join(sector(t['lba'] + k) for k in range(t['sectors']))
    got = body[:len(sbs)] if t['method'] == 0 else lz4_block(body[t['pad']:])
    check('INTRO.SBS = the file', t['size'] == len(sbs) and got == sbs)
check('header margin covers LZ4 in place', (margin_byte + 1) * 16 >= need_margin, (margin_byte, need_margin))
done()
