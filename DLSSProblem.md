# RAM growth on menu/map — investigation & fix

## Symptom

Every time the inventory or map was opened/closed, process RAM usage (PrivateBytes) grew
permanently — with DLSS ~3–7 GB per cycle, cumulative until the system froze. Opening/closing
the **Galaxy Star Map** in particular reproducibly caused an immediate crash.

Without DLSS (no upscaling or FSR3): no growth.

## There were TWO independent problems

Diagnosis revealed two distinct causes, both of which appeared as "RAM grows on menu":

1. **DLSS/Streamline viewport accumulation** — unbounded growth until freeze.
2. **AER `pastBuffer` reallocation on render-target size change** — a brief GPU-memory spike
   that crashed on the Star Map.

The **map crash** came from problem 2, not from DLSS. Problem 1 caused the slow, cumulative growth.

---

## Problem 1 — DLSS/Streamline viewport cycling

### Cause

On every menu cycle, Starfield issues a fixed Streamline sequence:

```
slDLSSSetOptions(viewport N,   mode=eOff,  output=INVALID)   // disable DLSS
slDLSSSetOptions(viewport N+1, mode=2,     output=6856x5904)  // re-initialize DLSS
slFreeResources (viewport N)                                   // free the old context
```

The viewport ID increases monotonically (0 → 1 → 2 → 3 → ...). Streamline keeps separate
GPU resource pools per viewport ID and does **not** return freed committed memory to the OS.
Every new viewport ID + every `slDLSSSetOptions(mode=eOff)` teardown + every `slFreeResources`
triggers an internal realloc → PrivateBytes grows without bound.

Additionally, Starfield called `slDLSSSetOptions(mode=2)` with identical parameters **every
frame** (~70×/s). Without filtering, this flooded Streamline with reinits → stutter/lag.

### Fix — `extern/vrframework/src/nvidia/UpscalerAfrNvidiaModule.cpp`

Redirect all DLSS calls to a single stable viewport (`0`, or AFR viewport `1025` on even
frames) and suppress Streamline teardown/realloc:

- **`on_dlssSetOptions`**:
  - `mode == eOff` → suppress entirely (`return eOk`). Prevents internal teardown.
  - Same resolution as last time → suppress. Prevents the 70×/s reinit flood (lag fix).
    The comparison uses a single `std::atomic<uint64_t>` (width<<32 | height) so the
    check-and-set is race-free across multiple render threads. (An earlier version with two
    `uint32_t` had a race condition → duplicate reinits.)
  - `on_device_reset` sets `s_dlss_options_reset = true`, forcing a single pass-through after
    a device reset.
  - Real calls → remap to viewport 0 (+ AFR 1025).
- **`on_slFreeResources`** (for the DLSS feature) → suppress (`return eOk`). No explicit free.
- **`on_slSetTag` / `on_slSetConstants` / `on_slEvaluateFeature`** → rewrite the viewport handle
  in the inputs to `0` (odd frames) or `1025` (even frames, AFR).
- **`on_slAllocateResources`** → also remap to viewport 0 (but Starfield does not call this
  explicitly — allocation happens lazily through `slEvaluateFeature`).

### Result

DLSS runs permanently on a single stable viewport. RAM no longer accumulates — it only
oscillates between ~21.8 GB and ~27 GB per menu cycle (Streamline reallocates internally for a
moment on `mode=2` and releases it again after menu close). No more lag.

### Important: do NOT suppress only partially

Suppressing `slFreeResources` but letting `slDLSSSetOptions(mode=eOff)` through → **crash**:
Streamline tears the viewport down internally, and the next `slEvaluateFeature` then runs on an
uninitialized viewport → access violation. Both must be suppressed together.

---

## Problem 2 — AER `pastBuffer` reallocation (the map crash)

### Cause — `src/CreationEngine/CreationEngineRendererModule.cpp`

For the alternate-eye-rendering motion-vector correction, the mod keeps copies of the render
targets in `m_pastBuffer[12][4]` (committed D3D12 resources). `ValidateResource` checks per
frame whether the size of the current render target still matches the copy; on a mismatch all 4
buffers were reset and **reallocated**.

Different menus render at **different** resolutions:
- Normal gameplay VR: `6856×5904`
- Galaxy Star Map: `6856×3856` (different height!)
- Inventory: `1988×1712` / `3428×2952`

When opening the map, the render targets shrank → old (`6856×5904`) buffers were freed and new
(`6856×3856`) ones allocated. When closing the map, the reverse. During the GPU reset the
**old + new buffers were committed simultaneously** → ~6 GB peak (up to ~30 GB) → crash.

### Fix

In `ValidateResource`: if the current render target is **smaller-or-equal** to the existing
copy (same format), skip the reallocation and omit the copy for this frame:

```cpp
if (desc.Format == desc2.Format && desc.Width <= desc2.Width && desc.Height <= desc2.Height) {
    return false;
}
```

The pastBuffers thus keep their maximum (`6856×5904`) size. On menu close, the returning
full-screen target fits again → no mismatch, no reallocation, no spike, no crash. The guard is
universal — it covers the Star Map and all smaller menu render targets.

---

## Side findings (fixed separately)

- **Null-pointer crash on save load** — `src/CreationEngine/CreationEngineCameraManager.cpp`,
  `onScaleformSetViewPortInternal`: `cc->GetMovieDef()` can be null during loading.
  Fix: `if (!cc || !cc->GetMovieDef()) return;`.

- **`\0` bytes in the log** — `extern/vrframework/src/mods/vr/runtimes/OpenXR.cpp`,
  `get_result_string` / `get_structure_string`: the `std::string` was resized to the max size
  (`XR_MAX_RESULT_STRING_SIZE`), but `xrResultToString` only writes a short null-terminated
  C string. spdlog then printed the whole buffer including the `\0` bytes.
  Fix: `.resize(strnlen(...))` after the API call.

- **File logging for RelWithDebInfo** — `CMakeLists.txt` + `extern/vrframework/src/Framework.cpp`:
  `ENABLE_FILE_LOGGING` is set for the `RelWithDebInfo` config so test builds (`smoke.ps1`
  builds RelWithDebInfo) write a `vr_log.txt`. The shipping build (`release-no-debug`) keeps the
  null logger — no impact on releases.

---

## Discarded approaches

- **Remapping the viewport ID to 0 in `slFreeResources`/`slAllocateResources`** alone:
  RAM kept growing — Streamline reuses no memory, even for the same ID.
- **`galaxyMapActive` flag** to skip `SwapBuffer` only during the Star Map:
  did not work reliably (the TAA and frame passes run before the Scaleform menu detection, so
  the flag was one frame late; there are also three separate `SwapBuffer` calls). Replaced by the
  universal shrink guard in `ValidateResource`, which works independently of menu detection.
- **Same-resolution suppression with two `uint32_t`**: race condition between render threads →
  duplicate reinits. Replaced by a single `std::atomic<uint64_t>`.

## Changed files (final fix)

| File | Change |
|---|---|
| `extern/vrframework/src/nvidia/UpscalerAfrNvidiaModule.cpp` | DLSS hooks: eOff/same-res suppression, viewport-0 remap, free suppression |
| `src/CreationEngine/CreationEngineRendererModule.cpp` | `ValidateResource` shrink guard (map-crash fix) |
| `src/CreationEngine/CreationEngineCameraManager.cpp` | Null guard against save-load crash |
| `extern/vrframework/src/mods/vr/runtimes/OpenXR.cpp` | `strnlen` trim against `\0` in the log |
| `extern/vrframework/src/Framework.cpp`, `CMakeLists.txt` | File logging for RelWithDebInfo |

## Open questions

- **Why does Starfield reinitialize DLSS on every menu cycle?** Presumably it interprets our
  brief `SetWindowSize` call as a resolution change. Not conclusively determined — but made
  harmless by the suppression.
- **~5 GB oscillation per cycle** remains: Streamline reallocates internally on
  `slDLSSSetOptions(mode=2)`. Cannot be reduced further from the hook layer without corrupting
  the DLSS state.
- **FSR3 (10 FPS)**: separate performance problem, not a RAM leak. Not investigated.
