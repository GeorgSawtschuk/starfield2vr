# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

`starfield2vr` is a Windows-only VR mod for Starfield. It builds as a proxy `dxgi.dll` that is injected when placed in the game's install directory. Based on praydog's REFramework. No tests — "verification" means it compiles and loads in-game.

## Submodule Setup (Required Before First Build)

`git submodule update --init --recursive` **partially fails** — `extern/vrframework` points to a deleted repo. Fix it per-clone:

```
git submodule init extern/vrframework
git config submodule.extern/vrframework.url https://github.com/Fribur/vrframework.git
git submodule update --recursive extern/vrframework
```

`extern/CommonLibSF` clones normally. All presets set `USE_STARFIELD_SDK_LITE=ON`, so CommonLibSF is NOT compiled — game types come from `sdk-lite/include` instead.

## Build

Requires MSVC toolchain + Ninja + CMake 3.27+. The presets hardcode `C:/Program Files/CMake/bin/cmake.exe`.

```
# Configure
cmake --preset build-release-no-debug-msvc

# Build (output: dxgi.dll)
cmake --build build/build-release-no-debug-msvc
```

POST_BUILD copies `dxgi.dll` directly into `C:\Program Files (x86)\Steam\steamapps\common\Starfield\`. Xbox Store variant: use preset `build-release-no-debug-msvc-xbox` (copies to `C:\XboxGames\Starfield\Content\`).

**Available presets** (configure/build pairs):
- `build-release-no-debug-msvc` / `release-no-debug-msvc` — CI build, ships this
- `build-release-msvc` / `release-msvc` — RelWithDebInfo (has PDB)
- `build-debug-msvc` / `debug-msvc` — Debug
- `build-release-no-debug-clang-cl` / `release-no-debug-clang-cl` — clang-cl release
- Xbox variants of clang-cl and msvc release-no-debug presets

**Format code** (WebKit base, 180-col, 2-space indent, CRLF):
```
clang-format -i <file>
```

## Architecture

### Two-Layer Design

**`extern/vrframework/`** — game-agnostic VR framework (D3D11/D3D12 hooks, OpenXR/OpenVR, ImGui overlay, `Mod` plugin system, `g_framework` singleton). See `extern/vrframework/CLAUDE.md` for its full architecture and callback lifecycle.

**`src/`** — Starfield-specific consumer. Entry point is `DllMain` → spawns a thread → `Sleep(5000)` startup delay → constructs `g_framework`. The 5-second delay is intentional to let the game fully initialize before hooking.

### `src/CreationEngine/` — Starfield Integration

`CreationEngineEntry` is the `Mod` subclass that registers with vrframework's `Mod` plugin system and installs all Starfield-specific hooks:

| File | Responsibility |
|------|---------------|
| `CreationEngineCameraManager` | VR camera transforms, 6DoF head tracking, decoupled pitch |
| `CreationEngineDirectX12Module` | D3D12-specific hooks into the Creation Engine renderer |
| `CreationEngineRendererModule` | Renderer integration, desktop recording fix |
| `CreationEngineInputManager` | Controller input mapping (XInput → ViGEm virtual gamepad) |
| `CreationEngineGameLoop` | Game loop hooks |
| `CreationEngineWeaponModule` | Haptic feedback on weapon events |
| `CreationEngineSettings` / `GameSettingsComponent` | In-game settings (HUD scale, resolution scale, world scale) |
| `memory/offsets.h`, `offsets_table.h` | Starfield memory addresses — these are game-version-dependent |
| `models/GameFlow.h` | `GameFlow::gStore` — runtime config state |
| `models/ModSettingsStore.h` | Persistent settings (serialized via vrframework's config system) |

`SIGNATURE_SCAN=ON` (default) means addresses in `src/CreationEngine/memory/` are resolved at runtime from byte patterns. When Starfield updates, these offsets must be re-found.

### Key Compile Definitions (set in CMakeLists.txt via `common` INTERFACE)

- `HOOK_DX12_FIRST` — try D3D12 before D3D11
- `HOOK_XINPUT` — intercept XInput to remap controller buttons
- `DISABLE_WINDOW_SIZE_FIXES` — skip vrframework window resize handling
- `GLM_ENABLE_EXPERIMENTAL` — needed for glm experimental extensions

Commented-out but available:
- `MOTION_VECTOR_REPROJECTION` — DLSS/FSR motion vector fix
- `VRMOD_EXPERIMENTAL` — extra D3D12 hooks (SetRenderTargets, etc.)
- `DEBUG_PROFILING_ENABLED` — scope profiler

### Math Convention

GLM in left-handed / DirectX convention. `GLM_FORCE_LEFT_HANDED` is asserted in `Main.cpp` at startup (logs and asserts coordinate handedness).

## Code Style

- No comments unless explaining something genuinely non-obvious (hardware quirks, API workarounds, hook offsets that can't be named). See `extern/vrframework/CLAUDE.md` for the strict rule.
- Column limit: 180. 2-space indent, no tabs. CRLF line endings.
- `std::unique_ptr` and `Microsoft::WRL::ComPtr` for resource management.
