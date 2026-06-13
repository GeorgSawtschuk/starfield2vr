---
name: run-starfield2vr
description: build, verify, and deploy the starfield2vr VR mod DLL; run smoke test; check artifact exports
---

starfield2vr is a **Windows DLL mod** — there is no runnable binary or server.
"Running" it means building `dxgi.dll`, verifying its DXGI exports, and deploying
it into Starfield's install directory. The game then loads the DLL on startup.

The driver for all of this is `.claude/skills/run-starfield2vr/smoke.ps1`.

---

## Prerequisites

- Visual Studio 2022 Community with **"Desktopentwicklung mit C++"** workload
  (installs MSVC, Ninja, Windows SDK, `fxc.exe`, and `vswhere.exe`)
- CMake 3.27+ installed to `C:\Program Files\CMake\bin\cmake.exe`
- Git (for submodule init)

First-time submodule setup (the `vrframework` URL in `.gitmodules` is broken;
use Fribur's repo):

```powershell
git submodule set-url extern/vrframework https://github.com/Fribur/vrframework.git
git submodule update --init --recursive
```

---

## Agent path — smoke.ps1

Run from the repo root:

```powershell
powershell -ExecutionPolicy Bypass -File .claude\skills\run-starfield2vr\smoke.ps1
```

What it does in order:

1. Locates Visual Studio via `vswhere.exe`
2. Patches DirectXTK/DirectXTK12 `CMakeLists.txt` files for CMake 4.x compat (see Gotchas)
3. Runs `cmake --preset build-release-msvc -DCMAKE_POLICY_VERSION_MINIMUM=3.5`
4. Runs `cmake --build --preset release-msvc`
5. Verifies `artifact/ModRelease/sfvr/dxgi.dll` exists and has >= 3 `CreateDXGIFactory` exports

Expected output on success:

```
[PASS] Build and verify complete.
Deploy: copy the DLL to Starfield's install dir as dxgi.dll
  Steam : C:\Program Files (x86)\Steam\steamapps\common\Starfield\dxgi.dll
  Xbox  : C:\XboxGames\Starfield\Content\dxgi.dll
```

Optional parameters:

```powershell
# Debug build
powershell -ExecutionPolicy Bypass -File .claude\skills\run-starfield2vr\smoke.ps1 `
    -Preset debug-msvc -ConfigPreset build-debug-msvc
```

---

## Manual build (human path)

Open **VS 2022 Developer Command Prompt**, then from the repo root:

```cmd
cmake --preset build-release-msvc -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build --preset release-msvc
```

The post-build step auto-copies the DLL to
`C:\Program Files (x86)\Steam\steamapps\common\Starfield\dxgi.dll`.
Then launch Starfield — the mod loads automatically and adds an F11 overlay.

---

## Gotchas

**CMake 4.x breaks MinHook configure**
Always pass `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` to every configure call.
Without it, configure aborts with a `cmake_minimum_required` version error.

**CMake 4.x breaks DirectXTK shader compilation**
`cmake -E env ... CompileShaders.cmd` can no longer launch `.cmd` files directly.
The `smoke.ps1` patches both `_deps/directxtk-src/CMakeLists.txt` and
`_deps/directxtk12-src/CMakeLists.txt` before each build. These patches are
lost when the build directory is deleted — `smoke.ps1` reapplies them automatically.

Patch applied (around line 232 in directxtk-src, line 266 in directxtk12-src):
```cmake
# Before:
... CompileShaders.cmd ARGS ${ShaderOpts}
# After:
... cmd.exe /C "${PROJECT_SOURCE_DIR}/Src/Shaders/CompileShaders.cmd" ${ShaderOpts}
```

**MSVC/CMake/Ninja are not in PATH**
All build commands must run inside a VS Developer environment.
`smoke.ps1` handles this via `vcvars64.bat` automatically.
Manually: use the "VS 2022 Developer Command Prompt" shortcut.

**vrframework submodule URL is broken**
`.gitmodules` points to `mutars/vrframework` which does not exist.
Always use `https://github.com/Fribur/vrframework.git` instead (see Prerequisites above).

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| `vswhere.exe not found` | Install Visual Studio 2022 |
| `cmake.exe not found at C:\Program Files\CMake\bin\cmake.exe` | Install CMake separately from cmake.org |
| Configure fails: `cmake_minimum_required ... Compatibility with CMake < 3.5 has been removed` | Pass `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` |
| Build fails: `'CompileShaders.cmd' is not recognized` | Re-run `smoke.ps1` — it patches the DirectXTK CMakeLists.txt before building |
| Build fails: `remote: Repository not found` on vrframework | Run the submodule URL fix in Prerequisites |
| DLL exists but no exports | Check the hook method: default is `dxgi`. Rebuild with correct `HOOK_METHOD`. |
