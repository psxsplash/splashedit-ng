# Parity tests against Unity SplashEdit 2.4.0

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
| c03 textures 4/8/16 bpp, exact palettes | identical | identical | identical |
| c04 textures needing k-means (>16 / >256 colours) | identical | palettes differ | identical |
| c05 colliders none/static/dynamic | nav region missing (not ported) | identical | identical |
| c06 baked directional + point light, sphere BVH | identical | identical | identical |
| c07 two submeshes, textured + tinted | identical | identical | identical |

Open: c04's k-means palettes. The C# quantizer from 2.4.0, run on Unity's bundled Mono over the
same dumped PNG, reproduces the C++ palettes exactly. So either the input the editor quantized or
the editor's runtime differs from that replay. The next step is a harness case that dumps
`GetPixels()` at quantize time.
