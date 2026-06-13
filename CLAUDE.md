# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**starfield2vr** is a VR mod for Starfield (Creation Engine) delivered as a Windows DLL (`dxgi.dll` / `dinput8.dll`). It provides 6DoF head tracking, motion controller input, and stereo rendering by hooking deep into the game engine at runtime.

## Build System

### Required Tools

| Tool | Where to get | Notes |
|---|---|---|
| **Visual Studio 2022** (Community) | visualstudio.microsoft.com | Workload: "Desktopentwicklung mit C++" |
| **CMake 3.27+** | cmake.org | Installs to `C:\Program Files\CMake\bin\cmake.exe` — NOT in PATH by default |
| **Ninja** | Included with VS workload | NOT in PATH by default outside Developer Prompt |
| **Windows SDK 10.0.26100+** | Included with VS workload | Provides `fxc.exe` shader compiler |
| **Git** | git-scm.com | For submodule initialization |
| **vswhere.exe** | Included with VS Installer | At `C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe` — used to locate VS installations programmatically |

Visual Studio installs the C++ compiler (`cl.exe`), Ninja, Windows SDK, and `fxc.exe` all in one go — no separate downloads needed beyond VS itself.

### First-Time Setup

```powershell
# 1. Fix broken submodule URL (mutars/vrframework doesn't exist)
git submodule set-url extern/vrframework https://github.com/Fribur/vrframework.git

# 2. Initialize submodules
git submodule update --init --recursive
```

### Building

MSVC tools and CMake are **not in PATH** by default — the build must run inside a VS Developer environment. Use PowerShell:

```powershell
# Find VS install path via vswhere
$vsPath = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
$vcvars = "$vsPath\VC\Auxiliary\Build\vcvars64.bat"
$cmake   = "C:\Program Files\CMake\bin\cmake.exe"

# Configure (add policy flag for CMake 4.x compatibility)
cmd /c "`"$vcvars`" && `"$cmake`" --preset build-release-msvc -DCMAKE_POLICY_VERSION_MINIMUM=3.5"

# Build
cmd /c "`"$vcvars`" && `"$cmake`" --build --preset release-msvc"
```

Alternatively use the **VS 2022 Developer Command Prompt** shortcut, where everything is already in PATH:
```
cmake --preset build-release-msvc -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build --preset release-msvc
```

Output lands in `artifact/ModRelease/sfvr/dxgi.dll` and is auto-copied to `C:\Program Files (x86)\Steam\steamapps\common\Starfield\dxgi.dll`.

**Other presets**: `debug-msvc`, `release-no-debug-msvc`, `release-no-debug-msvc-xbox`, and `clang-cl` variants of each.

**Key CMake flags** (set in `CMakePresets.json` base):
- `USE_STARFIELD_SDK_LITE=ON` — uses `sdk-lite/` instead of full CommonLibSF
- `SIGNATURE_SCAN=ON` — enables pattern-based offset discovery at startup
- `HOOK_METHOD=dxgi|dinput8|PHYSX|inject` — selects DLL injection strategy
- `XBOX_STORE=ON` — copies to Xbox Game Pass path instead of Steam

### Known Build Issues (CMake 4.x)

**CMake 4.x broke two dependencies** — both require patches in the `_deps` directory after the first configure. These patches are lost if the build directory is deleted (clean reconfigure re-downloads dependencies).

**1. MinHook** — configure fails without the policy flag:
```
# Always pass this during cmake --preset configure:
-DCMAKE_POLICY_VERSION_MINIMUM=3.5
```

**2. DirectXTK / DirectXTK12 shader compilation** — `cmake -E env ... CompileShaders.cmd` silently fails in CMake 4.x because `.cmd` files can't be launched directly (need `cmd.exe /C`).

Patch `build/build-release-msvc/_deps/directxtk-src/CMakeLists.txt` line ~232:
```cmake
# Before (broken):
COMMAND ${CMAKE_COMMAND} -E env CompileShadersOutput="${COMPILED_SHADERS}" CompileShaders.cmd ARGS ${ShaderOpts}
# After (fixed):
COMMAND ${CMAKE_COMMAND} -E env CompileShadersOutput="${COMPILED_SHADERS}" cmd.exe /C "${PROJECT_SOURCE_DIR}/Src/Shaders/CompileShaders.cmd" ${ShaderOpts}
```

Apply the same fix to `_deps/directxtk12-src/CMakeLists.txt` line ~266 (same pattern, same replacement). Then re-run configure + build.

## Architecture Overview

### Initialization Chain

```
DllMain() → new thread (5s delay) → Framework::Framework()
  → Framework registers Mods in order (ModConfig.cpp):
      1. VRConfig::get()               — OpenXR/OpenVR init
      2. VR::get()                     — pose tracking, haptics, XInput spoof
      3. UpscalerAfrNvidiaModule::Get() — NVIDIA upscaler compat
      4. CreationEngineEntry::Get()    — main Starfield mod
      5. GameSettingsComponent::Get()  — game settings
  → on_initialize_d3d_thread() called on D3D thread for each Mod
  → CreationEngineEntry::on_initialize() installs all engine hooks
```

### Mod Base Class (`extern/vrframework/src/Mod.hpp`)

Every feature is a `Mod` subclass. Key virtual lifecycle methods (called by Framework):

```cpp
virtual std::optional<std::string> on_initialize();
virtual std::optional<std::string> on_initialize_d3d_thread();
virtual void on_pre_imgui_frame();   // Input override point
virtual void on_frame();             // Per-frame, ImGui allowed
virtual void on_present();           // At Present(), NO ImGui
virtual void on_post_present();      // After Present returns
virtual void on_draw_ui();           // F11 overlay rendering
virtual void on_config_load(const utility::Config&, bool set_defaults);
virtual void on_config_save(utility::Config&);
// D3D12 command list hooks:
virtual void on_d3d12_set_render_targets(...);
virtual void on_d3d12_set_viewports(...);
virtual void on_d3d12_set_scissor_rects(...);
```

### Function Hook Pattern

All engine hooks follow this exact pattern:

```cpp
// In header:
std::unique_ptr<FunctionHook> m_hook;
static ReturnType callback(Params...);

// In InstallHooks():
REL::Relocation<uintptr_t> addr{ MemoryOffsets::SomeFunction() };
m_hook = std::make_unique<FunctionHook>(addr.address(), reinterpret_cast<uintptr_t>(&callback));
m_hook->create();

// In callback:
static ReturnType callback(Params... args) {
    auto self = ManagerClass::Get();
    using func_t = decltype(callback);
    static auto original = self->m_hook->get_original<func_t>();
    // custom logic
    return original(args...);
}
```

`FunctionHook` is from `extern/vrframework/src/memory/FunctionHook.h` and patches function prologues with trampolines.

### Config/UI Values (`ModValue` templates)

Settings shown in the F11 overlay are declared as typed `ModValue` smart pointers — they auto-persist to config files:

```cpp
const ModSlider::Ptr m_hud_scale{ ModSlider::create("CreationEngine_HUDScale", 0.1f, 1.0f, 0.4f) };
const ModToggle::Ptr m_decoupled_pitch{ ModToggle::create("CreationEngine_DecoupledPitch", false) };
const ModCombo::Ptr  m_dominant_eye{ ModCombo::create("CreationEngine_DominantEye", {"Right","Left"}, 0) };

ValueList m_options{ *m_hud_scale, *m_decoupled_pitch, *m_dominant_eye /* ... */ };

void on_config_load(const utility::Config& cfg, bool set_defaults) {
    for (IModValue& opt : m_options) opt.config_load(cfg, set_defaults);
}
```

Types: `ModToggle` (bool), `ModSlider` (float), `ModSliderInt32` (int), `ModCombo` (int index), `ModKey` (key binding), `ModString`.

## Core Modules

### CreationEngineEntry (`src/CreationEngine/CreationEngineEntry.cpp`)

Main mod. `on_initialize()` drives all hook installation:
```cpp
CreationEngineCameraManager::Get()->InstallHooks();
CreationEngineRendererModule::Get()->InstallHooks();
CreationEngineInputManager::Get()->Init();
```

### CreationEngineCameraManager

Hooks for head tracking. Functions hooked and their IDs:

| Hook | vtable / ID | What it does |
|---|---|---|
| `onNiAVObjectUpdateWorld` | BSFadeNode vtable[79] (ID 472039) | World transform updates for camera nodes |
| `onFPSGetCameraRotation` | FirstPersonState vtable[13] (ID 459617) | Replaces camera rotation quaternion with VR head pose |
| `onSetNimFrustum` | NiCamera vtable (ID 147392) | Modifies frustum for stereo |
| `onCalcNiFrustum` | NiCamera vtable (ID 147416) | Adjusts FOV for IPD/eye separation |
| `onScaleformSetViewPort` | Scaleform::Movie vtable[12] (ID 303817) | Scales HUD viewport |

Havok/Nirn coordinate conversion uses permutation matrices:
```cpp
permutation_pre = {1,0,0,0; 0,0,1,0; 0,-1,0,0; 0,0,0,1}
to_havok_space(mat) = permutation_pre * mat * permutation_post
```

### CreationEngineRendererModule

D3D12 render pipeline hooks:

| Hook | ID | Signature |
|---|---|---|
| `onRenderGraphRenderStart` | 143812 | `__int64(RenderGraph*, RenderGraphData*, __int64, __int64)` |
| `onUpdateConstantBufferView` | 142800 | `uintptr_t(uint8_t, uint8_t, uintptr_t×3, double, char, RenderPassConstantBufferView*)` |
| `onTaaPass` | 497712 | TAA vfunc7, prevents incompatible upscaling |
| `setReflexMarkerInternal` | 141825 | `uintptr_t(uintptr_t, uint32_t marker, uint32_t oldFrameIndex)` |

`RenderPassConstantBufferView` key offsets: `+0x50` projection matrix, `+0xD0` view matrix, `+0x268/0x26C` FOV halves, `+0x270/0x274` near/far planes.

Frame history: `m_pastBuffer[12][4]` — 12 render stages × 4 resources for motion vector reprojection.

### CreationEngineInputManager

Hooks `BSPCGamepadDevice::vftable[2]` (ID 470133). Flow:
1. Game polls gamepad → hook intercepts
2. `VR::get()` queried for controller button/axis state  
3. ViGEmBus virtual Xbox 360 pad updated via `vigem_target_x360_update()`
4. Spoofed `XINPUT_STATE` returned to game

### CreationEngineWeaponModule

Corrects weapon muzzle/laser positioning. Static offset `0x37de714` (not ID-based — manual update required on patch). Applies roll/pitch/yaw from `GameFlow::gState.debugWeaponData` to `NiNode->local.rotate` via GLM mat3x4 rowMajor conversion.

### HavokModule

Physics ray-cast corrections. Currently disabled (hooks commented out). Static offsets: `onHavokRaycast` at `0x1a276ac`, `onHavokRaycast2` at `0x2fdfdbc`.

## Memory & Offset System

### Three-Tier Resolution (`src/CreationEngine/memory/offsets.h`)

```cpp
// Tier 1: Pattern scan (debug / SIGNATURE_SCAN builds)
FuncRelocation(pattern_bytes, static_offset, ID)
VTable(name, pattern_bytes, static_offset)
InstructionRelocation(pattern, offset_begin, instruction_size, static_offset, ID)
  // RIP-relative: addr + *(int32_t*)(addr + offset_begin) + instruction_size

// Tier 2: Static offsets from offsets_table.h (release builds)
// Tier 3: REL::ID fallback (CommonLibSF legacy)
```

### Updating Offsets After a Starfield Patch

1. Build debug: `cmake --preset build-debug-msvc && cmake --build --preset debug-msvc`
2. Launch Starfield — check spdlog for `"offset mismatch: scanned=0xXXXX static=0xYYYY"`
3. Update `src/CreationEngine/memory/offsets_table.h`:
   ```cpp
   {ID, NEW_STEAM_OFFSET, NEW_XBOX_OFFSET}
   ```
4. Static offsets with no ID (weapon module, Havok) require manual IDA/x64dbg investigation

## VR Framework (`extern/vrframework`)

### VR Class (`extern/vrframework/src/mods/VR.hpp`)

Key query methods used by input/camera code:
```cpp
VR::get()->get_left_joystick()         // glm::vec2
VR::get()->get_right_joystick()        // glm::vec2
VR::get()->is_action_active(action, hand)  // bool
VR::get()->get_eye_pose(eye_index)     // glm::mat4
VR::get()->get_ipd()                   // float
VR::get()->is_using_controllers()      // bool
```

OpenXR action paths follow `/actions/default/in/ActionName` (e.g. `/actions/default/in/Trigger`).

### Framework Class (`extern/vrframework/src/Framework.hpp`)

`g_framework` global singleton. D3D12 command list hooks flow through it before dispatching to each Mod's `on_d3d12_*` callbacks. Config persistence via `g_framework->request_save_config()`.

## Global State

### GameFlow (`src/CreationEngine/models/GameFlow.h`)

```cpp
namespace GameFlow {
    extern Settings gStore;   // HUD scale, perspective — persisted
    extern State    gState;   // Runtime: weapon data, menu state, ray-cast origins
    
    bool isShowingMenu();
    bool isAimingDownSights();
    bool isWeaponDrawn();
    bool isInFirstPerson();
}
```

Menu detection uses DJB2 hashes of `.swf` filenames. `gState.uiData.modulino` is a frame counter for double-buffering rendered menu count across two frames.

### ModConstants (`src/CreationEngine/CreationEngineConstants.h`)

Global config namespace — values written from ModValue UI bindings and read by camera/renderer modules directly. Key fields: `headTrackingMultiplier`, `dominantEye`, `headTrackingType`, `enabledResourcesToCopy[12]`, `NUM_STAGES`.

### CreationEngineSingletonManager

Static accessors for game engine singletons, resolved once at startup via pattern scanning:

```cpp
CreationEngineSingletonManager::GetSceneGraphRoot()       // RE::Main::SceneGraphRoot* (ID 936470)
CreationEngineSingletonManager::GetPlayerCameraSingleton() // RE::PlayerCamera*         (ID 937788)
CreationEngineSingletonManager::GetPlayerRef()             // RE::PlayerCharacter*       (ID 922868)
```

## Submodule Notes

- **`extern/vrframework`**: The `.gitmodules` URL (`mutars/vrframework`) is broken. Working URL is `https://github.com/Fribur/vrframework.git`. If re-cloning, use: `git submodule set-url extern/vrframework https://github.com/Fribur/vrframework.git && git submodule update --init extern/vrframework`
- **`extern/CommonLibSF`**: Only used when `USE_STARFIELD_SDK_LITE=OFF`. Default builds use `sdk-lite/include/` instead.

## Coordinate System

Left-handed (`GLM_FORCE_LEFT_HANDED` in `pch/pch.h`). Verified at startup by `verifyLeftHandedCoordinates()` in `Main.cpp`. All matrix/quaternion operations must respect this — Havok space requires the permutation matrix conversion above.

## Known Limitations

- OpenVR controller bindings incomplete (prefer OpenXR)
- Frame Generation must be disabled in Starfield graphics settings
- Motion vector reprojection experimental (`MOTION_VECTOR_REPROJECTION` define, disabled by default)
- Static offsets in `CreationEngineWeaponModule` and `HavokModule` have no ID fallback — require manual update after patches
