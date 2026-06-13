# smoke.ps1 -- build starfield2vr and verify the output DLL
# Run from repo root:
#   powershell -ExecutionPolicy Bypass -File .claude\skills\run-starfield2vr\smoke.ps1

param(
    [string]$Preset       = "release-msvc",
    [string]$ConfigPreset = "build-release-msvc"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# 1. Locate tools
$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found. Install Visual Studio 2022." }

$vsPath = & $vswhere -latest -property installationPath
$vcvars = "$vsPath\VC\Auxiliary\Build\vcvars64.bat"
$cmake  = "C:\Program Files\CMake\bin\cmake.exe"
if (-not (Test-Path $cmake)) { throw "cmake.exe not found at $cmake" }

Write-Host "VS : $vsPath"
Write-Host "CMake: $(& $cmake --version | Select-Object -First 1)"

# 2. Patch DirectXTK CMakeLists for CMake 4.x compatibility
function Patch-DirectXTK([string]$cmakeFile) {
    if (-not (Test-Path $cmakeFile)) { return }
    $content = Get-Content $cmakeFile -Raw
    if ($content -notmatch 'CompileShaders\.cmd ARGS') { return }
    Write-Host "Patching $cmakeFile for CMake 4.x..."
    # Replace old invocation (cmake -E env ... CompileShaders.cmd ARGS)
    # with cmd.exe /C + full path (works on CMake 4.x)
    $content = $content -replace `
        '(\$\{CMAKE_COMMAND\} -E env CompileShadersOutput="\$\{COMPILED_SHADERS\}"(?:\s+\$<[^>]+>)*)\s+CompileShaders\.cmd ARGS', `
        '$1 cmd.exe /C "${PROJECT_SOURCE_DIR}/Src/Shaders/CompileShaders.cmd"'
    Set-Content $cmakeFile $content -Encoding UTF8 -NoNewline
    Write-Host "  Done."
}

$repoRoot = Split-Path (Split-Path (Split-Path $PSScriptRoot))
$buildDir = Join-Path $repoRoot "build\$ConfigPreset"

Patch-DirectXTK (Join-Path $buildDir "_deps\directxtk-src\CMakeLists.txt")
Patch-DirectXTK (Join-Path $buildDir "_deps\directxtk12-src\CMakeLists.txt")

# 3. Configure
Write-Host ""
Write-Host "=== Configure ($ConfigPreset) ==="
$out = cmd /c "`"$vcvars`" && `"$cmake`" --preset $ConfigPreset -DCMAKE_POLICY_VERSION_MINIMUM=3.5 2>&1"
if ($LASTEXITCODE -ne 0) {
    $out | Select-Object -Last 25 | Write-Host
    throw "Configure failed (exit $LASTEXITCODE)"
}
Write-Host ($out | Select-String "Configuring done|error" | Out-String).Trim()

# Re-patch in case configure re-fetched the deps
Patch-DirectXTK (Join-Path $buildDir "_deps\directxtk-src\CMakeLists.txt")
Patch-DirectXTK (Join-Path $buildDir "_deps\directxtk12-src\CMakeLists.txt")

# 4. Build
Write-Host ""
Write-Host "=== Build ($Preset) ==="
$out = cmd /c "`"$vcvars`" && `"$cmake`" --build --preset $Preset 2>&1"
if ($LASTEXITCODE -ne 0) {
    $out | Select-Object -Last 30 | Write-Host
    throw "Build failed (exit $LASTEXITCODE)"
}
Write-Host ($out | Select-String "Linking|error" | Out-String).Trim()

# 5. Verify artifact
Write-Host ""
Write-Host "=== Verify artifact ==="

$artifactDir = Join-Path $repoRoot "artifact\ModRelease\sfvr"
if ($Preset -match "debug") { $artifactDir = $artifactDir -replace "ModRelease","ModDebug" }
$dll = Join-Path $artifactDir "dxgi.dll"

if (-not (Test-Path $dll)) { throw "Output DLL not found: $dll" }

$sizeMB = [math]::Round((Get-Item $dll).Length / 1MB, 1)
Write-Host "DLL : $dll ($sizeMB MB)"

# Check expected DXGI exports
$msvcVer  = Get-ChildItem "$vsPath\VC\Tools\MSVC" | Sort-Object Name | Select-Object -Last 1 -ExpandProperty Name
$dumpbin  = "$vsPath\VC\Tools\MSVC\$msvcVer\bin\Hostx64\x64\dumpbin.exe"

if (Test-Path $dumpbin) {
    $exports      = & $dumpbin /exports $dll 2>&1 | Select-String "CreateDXGIFactory"
    $exportCount  = ($exports | Measure-Object).Count
    if ($exportCount -lt 3) { throw "Expected >= 3 CreateDXGIFactory exports, found $exportCount" }
    Write-Host "Exports: $exportCount CreateDXGIFactory entries -- OK"
} else {
    Write-Host "dumpbin not found -- skipping export check"
}

Write-Host ""
Write-Host "[PASS] Build and verify complete."
Write-Host "Deploy: copy the DLL to Starfield's install dir as dxgi.dll"
Write-Host "  Steam : C:\Program Files (x86)\Steam\steamapps\common\Starfield\dxgi.dll"
Write-Host "  Xbox  : C:\XboxGames\Starfield\Content\dxgi.dll"
