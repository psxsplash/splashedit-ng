# Unity parity harness (SplashEdit 2.4.0)

`NgParity.cs` builds the test scenes from code, exports each twice with the Unity 2.4.0 exporter, and
dumps the same scene in the splashedit-ng text format.

Setup (once):
1. Check out splashedit at tag `2.4.0` with Git LFS (the DotRecast DLLs are LFS objects; a pointer
   file breaks the compile): `git worktree add --detach <dir> 2.4.0`, then `git lfs pull` in it.
2. A Unity 6000.3 project whose `Packages/manifest.json` has
   `"net.psxsplash.splashedit": "file:<dir>"`, with `NgParity.cs` copied to `Assets/Editor/`.

Run headless with the Unity CLI (the first import takes minutes):

    unity build <project> --target StandaloneLinux64 --execute-method NgParity.Run \
      --args "-ngParityOut <out> [-ngParityCases c01_cube_flat,c03_textures]"

Output per case: `unity/` and `unity-run2/` (`scene.splashpack/.vram/.spu`), `ir/scene.scene` with
`ir/assets/*.mesh|png`, and `log-<run>.txt`. Also `summary.txt`.
