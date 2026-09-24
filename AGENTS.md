# AGENTS.md - EGL working contract

EGL is a portable EGL implementation: a thin C API surface in `src/egl.c`
forwarding to `_egl*` entry points, with the real implementation in
`src/egl_*.cpp`. Display/config/context/surface/sync/image object model with
deferred destruction and reference counting, over four native backends:

| Backend | Where | Notes |
|---|---|---|
| Windows WGL + Vulkan HDR | `src/egl_windows.cpp`, `src/egl_windows_vk.cpp` | fully implemented |
| Windows ANGLE | `src/egl_windows_angle.cpp` | OpenGL ES delegated to ANGLE's libEGL |
| X11 / GLX + optional Vulkan HDR | `src/egl_x11_glx.cpp`, `src/egl_linux_vk.cpp` | `LINUX_VK` option |
| Wayland (GLX via XWayland + Vulkan present) | `src/egl_wayland.cpp` | all presentation goes through Vulkan |
| Linux/Wayland system GLES | `src/egl_linux_gles.cpp` | delegated to the system libEGL |

**Stability mandate.** This implements a Khronos-specified API and is a drop-in
for other EGL implementations, so conformance is the contract.

- Never edit the upstream API headers. `include/` holds **only** the
  Khronos-generated/owned headers (`EGL/egl.h`, `EGL/eglext.h`,
  `EGL/eglplatform.h`, `KHR/khrplatform.h`) and `src/wglext.h` is likewise
  upstream-generated. If one needs a change, that is a discussion, not an edit.
- Every first-party header lives in `src/`. This includes `src/eglctxinternals.h`;
  keeping first-party headers out of `include/` is deliberate so the public
  include directory reads as exactly the standard EGL API surface.
- Match the EGL 1.5 error contract: the documented `EGL_BAD_*` code must be set
  on **every** failure path. Returning `EGL_FALSE` / `EGL_NO_*` without setting
  an error leaves `eglGetError()` reporting `EGL_SUCCESS` and is a defect.
- Behaviour must be identical across backends. A platform-divergent outcome
  (something accepted on Windows and rejected on GLX, or vice versa) is a bug
  even when each half is locally defensible.
- Never reformat. Formatting is authored, not derived; tooling runs with
  `FormatStyle: none`. `src/egl.c` and the platform files have distinct,
  deliberate styles.

## Build (standalone)

```
cmake -S . -B build/ninja -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/ninja
```

Requires the Vulkan SDK on Windows (`find_package(Vulkan REQUIRED)`); ANGLE
comes from the vcpkg manifest (`vcpkg.json`). Options: `EGL_WIN_ENABLE_ANGLE`,
`EGL_WAYLAND_ENABLE_GLES`, `EGL_LINUX_ENABLE_GLES`, `LINUX_VK`. Examples build
via `add_subdirectory(examples)`.

The build policy in `cmake/` is registered **only when EGL is the top-level
project**, so being vendored never disturbs a host build.

## Finding and eliminating bugs

All tooling is optional at build time - a clean checkout with none of it
installed builds exactly as before.

| Layer | Command | Gate |
|---|---|---|
| Compiler warnings | part of every build | `ENABLE_WERROR=ON` makes them fatal |
| cppcheck | `cmake --build build/ninja --target cppcheck` | exits non-zero on findings |
| cppcheck (exhaustive) | `... --target cppcheck-strict` | opt-in |
| clang-tidy | `python tools/check_tidy.py --build-dir build/ninja` | `WarningsAsErrors` in `.clang-tidy` |
| Sanitizers | `-DENABLE_SANITIZER=address,undefined` then run the examples | runtime faults |

`ENABLE_WERROR` is **OFF** by default so a clean checkout keeps building; turn
it on once the baseline is clean. `ENABLE_SANITIZER` (`address`, `undefined`,
`address,undefined`, `thread`) is the highest-yield lane here because the
object graphs are hand-rolled linked lists with reference counts.

**Suggested bug-elimination cycle.** Build with
`ENABLE_SANITIZER=address,undefined`, run the examples (especially the
`display_p3*` set and anything exercising `eglMakeCurrent`/`eglReleaseThread`),
run `cppcheck`, run `check_tidy.py`. Fix in order of confidence: memory
corruption and use-after-free first, then conformance/error-contract defects,
then platform divergences. Re-run the full set after each change. When anything
broken is found at any point, drop back to bugs immediately.

Suppression policy: `cppcheck.supp` stays short and every entry carries a
justification. Prefer inline `// cppcheck-suppress <id>` for one-off findings.
Never suppress to make a gate go green.

## Layout

| Path | Contents |
|---|---|
| `src/egl.c` | the public `egl*` C entry points, forwarding to `_egl*` |
| `src/egl_api.cpp`, `egl_globals.cpp`, `egl_display.cpp`, `egl_config.cpp`, `egl_context.cpp`, `egl_surface.cpp`, `egl_sync.cpp`, `egl_image.cpp` | the portable object model |
| `src/egl_internal.h`, `egl_common.h` | internal contracts shared by core and backends |
| `src/eglctxinternals.h` | first-party handle struct exposed to consumers (`eglGetPlatformDependentHandles`) |
| `src/egl_*.cpp`, `egl_*_vk.h` | the native backends listed above |
| `src/wglext.h` | upstream WGL extension declarations |
| `include/` | **upstream Khronos headers only** |
| `examples/` | one sample app per feature, incl. the Display-P3 colour-space trio |
| `cmake/` | build policy: `warnings.cmake`, `cppcheck.cmake` |
| `tools/` | `check_tidy.py` |

## Defect classes this code is prone to

- **Missing error codes.** Every failure return needs its `EGL_BAD_*`. This is
  the single most common defect here and it is invisible in testing unless
  `eglGetError()` is asserted.
- **Refcount asymmetry.** `_eglMakeCurrent` and the release paths must agree on
  when a surface is referenced twice (draw == read takes one reference, not
  two). An asymmetric decrement drives `refCount` to -1, and cleanup only frees
  at exactly 0 - so the object leaks forever, or a later release frees one that
  is still current. Check every increment/decrement pair.
- **Lock coverage.** The per-display list walk and the list mutation have to
  agree on the same mutex. Reading `rootCtx`/`rootImage`/`initialized` under
  only the global read lock while another entry point mutates under the display
  mutex is a data race.
- **Off-by-one on attribute-list caps.** A bound check placed *after* processing
  a pair rejects the last legal pair. The attribute templates are written by
  fixed slot, so the check is an input cap, not an overflow guard - it must
  fire after the final pair, not at it.
- **Native handle lifetime.** GDI object selection before `DeleteDC`, X11
  resources on error paths, `XFree` of a non-NULL zero-length array, and
  `XGetGeometry` return values (unchecked, they leave `width`/`height`
  uninitialized and a bad drawable kills the process under Xlib's default
  handler).
- **Extension-string accuracy.** Advertised extension names must be the
  registered ones, and an advertised capability must actually be implemented -
  a capability bit never set is a feature that can never be reached.
- **Sentinel return values from platform APIs.** `wglGetProcAddress` reports
  failure with `1`/`2`/`3`/`(PROC)-1`, not `NULL`. Every such wrapper needs the
  full check.

## Conventions

- **Production code discipline**: clean code and clean architecture, **no
  hacks**. No content sniffing, magic numbers, hidden special cases,
  duplicated blocks, or indirections that silently die.
- **Work priority is a strict cycle**: **bugs first**, then refactoring or new
  features. When anything broken is found at any point, drop back to bugs
  immediately.
- **Dependency licence gate**: check the licence for closed-source commercial
  use before adding anything. MIT / BSD / zlib / Apache-2.0 / Unlicense pass;
  copyleft and non-commercial fail; unclear means ask. Record it in
  `THIRD-PARTY.md` in the same change.
- **Comments explain *why*, not *what***. Spec citations and the reasoning
  behind a non-obvious workaround are worth keeping; do not narrate code.
- **Language**: English only, always.
- After any change, build clean and re-run the analysis set before considering
  the work done.
