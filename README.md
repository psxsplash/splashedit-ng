# SplashEdit NG

Standalone scene editor for [psxsplash](https://github.com/psxsplash/psxsplash), replacing the Unity-based SplashEdit. Plan and design decisions: psxsplash/splashedit#48.

## Building

Needs CMake 3.24+, a C++20 compiler, SDL3 and FreeType. Dear ImGui and stb are fetched at configure time.

```
cmake -S . -B build -G Ninja
cmake --build build
./build/splashedit
```

`--screenshot out.png [--size 1600x960] [--mouse x,y] [--clean]` renders a few frames and writes a PNG, which also works headless under `xvfb-run`.

Fonts: Inter (SIL OFL 1.1) and Lucide (ISC), licences in `assets/fonts`.

## Branding

`assets/brand` holds the psxsplash logo, icon and "made with" badges, copied from psxsplash/brand where `tools/build_brand.py` generates them. Use these for the window icon, splash/about screens and anything the exporter shows the user; don't draw new logos here. To change the branding, change the generator in psxsplash/brand and copy the output back. The old splat with the PlayStation logo must not be used anywhere.

- `icon.svg`, `icon-*.png`: window and app icon
- `logo.svg` (light backgrounds), `logo-dark.svg`/`logo-dark.png` (dark UI), `logo-mono.svg` (one colour)
- `made-with.svg`, `made-with-square.svg`: badges for exported games
- `bootscreen.svg`: 320x240 reference for the in-engine splash

Colours: `#ff3b3b #ffb400 #2ecc40 #1e90ff #b145ff #ff4fa3`, ink `#16122b`.
