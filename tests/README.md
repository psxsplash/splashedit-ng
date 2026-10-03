# Tests

## Format check against Unity SplashEdit 2.4.0

The exporter is not a port of 2.4.0 and does not try to match its output where 2.4.0 does worse
(textures, see below). The 2.4.0 exports are kept as a check that the file layout has not drifted.

`unity/NgParity.cs` builds small scenes in Unity, exports them with 2.4.0 and dumps each one as a
scene file (`unity/README.md` has the setup). `splashpack-cli` exports the dump, and
`splashdiff.py` compares the two section by section, matching objects by name.

    splashpack-cli export <out>/<case>/ir/scene.scene -o cpp/<case>.splashpack \
        --order-from <out>/<case>/unity/scene.splashpack
    python3 splashdiff.py <out>/<case>/unity/scene.splashpack cpp/<case>.splashpack
    cmp <out>/<case>/unity/scene.vram cpp/<case>.vram

`--order-from` copies Unity's object order. Unity's order comes from `FindObjectsByType` and changes
between runs, and the BVH leaf order depends on it through an unstable sort. With the order copied,
the files can be compared byte for byte.

| case | .splashpack | .vram | .spu |
|---|---|---|---|
| c01 cube, flat colour | identical | identical | identical |
| c02 rotations, non-uniform scale, mesh colours, inactive object | identical | identical | identical |
| c03 textures 4/8/16 bpp | CLUT length field only | texels and CLUTs differ (new quantizer) | identical |
| c04 textures needing a palette search | CLUT length field only | texels and CLUTs differ (new quantizer) | identical |
| c05 colliders none/static/dynamic, one nav region | identical | identical | identical |
| c06 baked directional + point light, sphere BVH | identical | identical | identical |
| c07 two submeshes, textured + tinted | identical | CLUT differs (new quantizer) | identical |

## Texture quality

`splashpack-cli texstats` converts one image the way export does and scores the result against
the source: RGB PSNR, mean OKLab deltaE (x100) per texel, and the same deltaE after a 3x3 box
blur. `--out` writes what the console will show.

    splashpack-cli texstats image.png --bpp 4 [--cutout] [--out decoded.png]

The quantizer differs from 2.4.0's in these ways:

- 5-bit channels are rounded to nearest at every bit depth. 2.4.0 truncated with `(ushort)(c * 31)`.
- If the image fits the palette once rounded to 15 bits, the palette is exact and nothing is
  dithered.
- Otherwise the palette is a k-means in OKLab over the distinct colours, weighted by the square
  root of their texel counts, seeded farthest-point first (deterministic) and iterated until it
  settles. 2.4.0 ran 10 rounds of unweighted RGB k-means from evenly spaced picks in scan order.
- Texels are error-diffused onto the palette after it is rounded to 15 bits, in serpentine order,
  at 0.75 strength.
- Opaque black is 0x8000. 2.4.0 wrote (1,1,1) with the STP bit.
- The CLUT is padded to a multiple of 4 words. 2.4.0 always added 1 to 4 more.

Mean deltaE, 2.4.0 against this exporter, on the test textures from `unity/NgParity.cs` and
sprite and UI art from a psxsplash game (images with transparency measured with `--cutout`):

| image | bpp | deltaE 2.4.0 | deltaE now | PSNR 2.4.0 | PSNR now |
|---|---|---|---|---|---|
| Amogus | 4 | 8.556 | 0.281 | 30.22 | 41.83 |
| Amogus | 8 | 8.385 | 0.169 | 31.16 | 47.91 |
| Band | 4 | 3.609 | 1.096 | 28.25 | 32.89 |
| Band | 8 | 1.938 | 0.652 | 33.71 | 41.08 |
| crew | 4 | 1.596 | 1.026 | 35.05 | 38.74 |
| crew | 8 | 1.596 | 1.026 | 35.05 | 38.74 |
| Nic | 4 | 3.138 | 1.292 | 27.70 | 32.39 |
| Nic | 8 | 4.567 | 0.928 | 33.92 | 39.63 |
| t03_pal100_32x32 | 4 | 8.431 | 6.323 | 18.86 | 18.51 |
| t03_pal100_32x32 | 8 | 1.312 | 0.692 | 35.27 | 40.58 |
| t03_pal7_16x16 | 4 | 0.261 | 0.037 | 41.69 | 58.60 |
| t03_pal7_16x16 | 8 | 0.261 | 0.037 | 41.69 | 58.60 |
| t03_rgb_32x16 | 4 | 6.655 | 4.846 | 21.51 | 21.33 |
| t03_rgb_32x16 | 8 | 1.527 | 1.233 | 33.39 | 32.67 |
| t04_grad_64x64 | 4 | 14.846 | 6.522 | 14.71 | 18.85 |
| t04_grad_64x64 | 8 | 2.604 | 1.681 | 28.87 | 29.97 |
| t04_many_32x32 | 4 | 9.649 | 4.982 | 15.47 | 21.15 |
| t04_many_32x32 | 8 | 2.011 | 1.254 | 29.69 | 31.59 |
| tileset | 4 | 1.627 | 0.637 | 34.69 | 42.75 |
| tileset | 8 | 1.627 | 0.637 | 34.69 | 42.75 |

deltaE goes down on every row. RGB PSNR drops slightly on three small synthetic rows because the
palette minimises OKLab distance. Not yet checked: these textures rendered
by psxsplash on the console or an emulator.

## Boot check

`boot/boot.sh` boots psxsplash (a PCdrv build) on one exported scene in pcsx-redux, headless, and
saves the screen at a given frame (default 600). The three files are copied to a temporary
directory as `scene_0.*`, the names the PCdrv loader opens.

    boot/boot.sh <pcsx-redux> <openbios.bin> <psxsplash.ps-exe> <dir>/scene shot.png [frame]

Without nav regions psxsplash does not attach the camera to the player, so the camera stays at the
origin facing +Z and the player start in the file has no effect. Put the objects at positive Z.

`boot/palcheck.py` checks one textured quad on the screenshot against `texstats --out`: every pixel
must be a palette colour, give or take the one 5-bit step the GPU dither adds.

    python3 boot/palcheck.py shot.png x0 y0 x1 y1 decoded.png

With c03 moved to z = 3 all three quads render, and the 4 and 8 bpp quads pass with 16-word
CLUT padding (half the pixels exact, half one dither step off). Against the wrong palette the
4 bpp quad fails with a distance of 90.

## Load and save

`roundtrip.py` resaves scene and mesh files with `splashpack-cli resave` and checks that known values
and unknown keys survive. Bad versions and malformed mesh arrays must fail to load.

    python3 roundtrip.py <splashpack-cli>

## Lua compiler

`luacheck.py` compiles every `lua/*.lua` with `splashpack-cli luac` and compares the result with
`lua/expected/*.luac` byte for byte. The expected files come from psxsplash's `tools/luac_psx`
running in pcsx-redux, so CI checks the host compiler without an emulator. The script also checks
that a changed source gives different bytes and that a syntax error is reported.

    python3 luacheck.py <splashpack-cli>
    splashpack-cli luac in.lua -o out.luac

The files cover constant folding with 32-bit overflow, division and modulo by negative constants, hex and
oversized literals, strings with escapes and zero bytes, long strings, more than 256 constants,
closures, varargs, goto, methods, loops and a table constructor large enough to need an extra
SETLIST argument.

To regenerate the expected files, list the sources in a `manifest.txt` (source path, then output
path, one per line) in a directory, and boot `luac_psx.ps-exe` with that directory as the PCdrv
base. It writes `__done__` containing `OK` when it has finished.

    pcsx-redux -testmode -stdout -safe -bios openbios.bin -loadexe luac_psx.ps-exe -run \
        -pcdrv -pcdrvbase <dir>

Two psxlua quirks the host build reproduces: `a ^ b` gives a to the power b + 1, and a decimal
numeral with a leading zero (`010`, `0.5`) is a "malformed number".

## Audio

`audiocheck.py` encodes the WAVs in `audio/` at their own rate and compares the ADPCM with
`audio/expected/`, which psxavenc (`-t spu -f <rate>`, plus `-L` for the looped files) wrote from the
same 16-bit mono PCM. The encoder is libpsxav's, as in psxavenc, so these match byte for byte.
Resampling is our own windowed sinc; psxavenc's goes through ffmpeg, so resampled clips differ.

    python3 audiocheck.py <splashpack-cli>

`splashpack-cli audio <in.wav> -o <out> --rate <hz> [--loop] [--trim] --pcm <src.wav>` writes the
PCM it starts from, which is what was fed to psxavenc to make the expected files.
