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
