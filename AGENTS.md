# AGENTS.md

Starfield VR mod (`starfield2vr`). Builds a proxy `dxgi.dll` that hooks the game. Windows-only, C++23, CMake + Ninja.

## Submodules (read first)

`git submodule update --init --recursive` partially FAILS: `.gitmodules` points `extern/vrframework` at `https://github.com/mutars/vrframework.git`, which no longer exists (404). The working fork is `https://github.com/Fribur/vrframework.git`. Fix per-clone without touching the tracked `.gitmodules`:

```
git submodule init extern/vrframework
git config submodule.extern/vrframework.url https://github.com/Fribur/vrframework.git
git submodule update --recursive extern/vrframework
```

`extern/CommonLibSF` clones fine from its recorded URL.

## Build

Use the presets in `CMakePresets.json` (generator is Ninja; presets hardcode `C:/Program Files/CMake/bin/cmake.exe`):

```
cmake --preset build-release-no-debug-msvc
cmake --build build/build-release-no-debug-msvc
```

- Build dir is `build/<configurePresetName>`.
- Default `HOOK_METHOD=dxgi` ⇒ output is `dxgi.dll` (a DLL proxy, not an exe).
- POST_BUILD copies `dxgi.dll` into the hardcoded Starfield install path (`C:\Program Files (x86)\Steam\steamapps\common\Starfield`, or `C:\XboxGames\...` when `XBOX_STORE=ON`). Builds touch those paths; expect failures/no-ops if Starfield isn't installed there.
- All presets set `USE_STARFIELD_SDK_LITE=ON`, so `extern/CommonLibSF` is NOT compiled — game types come from `sdk-lite/include`. CommonLibSF is only built when `USE_STARFIELD_SDK_LITE=OFF`.
- `SIGNATURE_SCAN=ON`: game addresses are resolved at runtime from byte patterns / ID offsets in `src/CreationEngine/memory/` (`offsets.h`, `offsets_table.h`). These are Starfield-build-version dependent.
- CI (`.github/workflows/dev-release.yml`) builds only the `release-no-debug-msvc` preset but with the `Visual Studio 17 2022` generator, then packages OpenXR/OpenVR zips (using `shipping/openxr_loader.dll` / `openvr_api.dll`). Tags trigger a GitHub release.

There are no tests. "Verification" = it compiles and loads in-game.

## Layout

- `extern/vrframework/` — generic, game-agnostic VR framework (praydog REFramework-based: D3D11/D3D12 hooks, OpenXR/OpenVR, ImGui overlay, `Mod` plugin system, `g_framework`). See `extern/vrframework/CLAUDE.md` for its architecture and callback lifecycle; do not duplicate that here.
- `src/` — the Starfield-specific consumer compiled into this DLL. `src/Main.cpp` is `DllMain` → spawns a thread (note the `Sleep(5000)` startup delay) → constructs `g_framework`.
- `src/CreationEngine/` — Creation Engine integration. `CreationEngineEntry` is the `Mod` subclass that installs camera/renderer/input hooks; settings live in `GameFlow::gStore` / `ModConstants`.
- `experimental/` — NOT compiled by the root `CMakeLists.txt` (no `add_subdirectory`/glob covers it). Scratch/disabled code despite the `SFVR_EXPERIMENTAL` preset var.
- `pch/PCH.h` is the precompiled header; `imgui/imconfig.hpp` is the ImGui user config.

## Conventions

- Format with `.clang-format` (WebKit base, 180-col, 2-space no tabs, CRLF). Run `clang-format -i` on changed files.
- No-comments rule (from `extern/vrframework/CLAUDE.md`): prefer self-documenting code; only comment genuinely non-obvious logic (hardware quirks, API workarounds, hook offsets).
- Math is GLM in left-handed / DirectX convention.
