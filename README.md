# EGL 1.5 Implementation

[![Build](https://github.com/McNopper/EGL/actions/workflows/build.yml/badge.svg)](https://github.com/McNopper/EGL/actions/workflows/build.yml)

A portable, spec-compliant implementation of [EGL
1.5](https://www.khronos.org/registry/egl/) built around a platform-agnostic
core and pluggable OS/rendering backends. The library provides the standard EGL
surface creation, context management, and synchronization API across operating
systems, so that applications can share a single EGL-based rendering path
regardless of the underlying platform.

The Khronos headers bundled with the library are the official, unmodified ones
from the [EGL Registry](https://github.com/KhronosGroup/EGL-Registry) (commit
e80a2e0050, 2026-03-19).

## License

The project is distributed under the **MIT License** — see [`LICENSE`](LICENSE).

The only third-party dependency is **ANGLE** (BSD-3-Clause, Google ANGLE Project
Authors), used only on Windows as the optional OpenGL ES backend and loaded at
runtime (not bundled in source). Its full license text and the permissive
licenses of ANGLE's own bundled components are documented in
[`THIRD-PARTY.md`](THIRD-PARTY.md). Non-Windows builds have no third-party
dependencies.

## Purpose

The primary goal is **portable HDR rendering**: write your application against
EGL 1.5 once, and let the library translate `EGL_GL_COLORSPACE_BT2020_PQ_EXT`,
`EGL_GL_COLORSPACE_SCRGB_LINEAR_EXT`, and the other HDR colorspaces into
whatever underlying stack the OS provides.

The same EGL call routes through completely different layers per platform:

| Layer | Windows | Linux Wayland |
| --- | --- | --- |
| Window system | Win32 / WGL | Wayland + xdg-shell |
| HDR signaling | DXGI swap chain HDR metadata | `wp_color_management_v1` protocol |
| Vulkan WSI | `VK_KHR_win32_surface` | `VK_KHR_wayland_surface` |
| HDR metadata API | `IDXGISwapChain4::SetHDRMetaData` | `vkSetHdrMetadataEXT` |
| GPU driver | NVIDIA / AMD / Intel Windows | NVIDIA 555+ / Mesa / RADV Linux |

Practical workflow this enables:

1. **Develop on Windows** — fast iteration, immediate HDR validation on a
   Windows HDR display
2. **Validate on Linux** — boot Fedora KDE (live USB or dual-boot), build the
   same source tree with `-DWL_EGL_PLATFORM=ON`, run the same example binaries,
   compare results
3. If colors differ, the bug is platform-specific (compositor, driver, or
   platform glue) — not the application

The naming convention encodes the variant in the executable filename and window
title (`bt2020_pq_GL_Windows_WGL_VK.exe` vs `bt2020_pq_GL_Linux_Wayland_VK`) so
that screenshot comparisons across platforms remain unambiguous.

## Platform and Backend Support

| Operating System | Windowing | Backend | HDR | Status |
| --- | --- | --- | --- | --- |
| Windows | WGL | Vulkan | ✅ | **Implemented** |
| Linux — X11 | GLX | — | ❌ (X11 has no HDR signaling) | **Implemented** |
| Linux — X11 + Vulkan | GLX | Vulkan (`-DLINUX_VK=ON`) | ⚠️ Driver/compositor dependent | **Implemented** |
| Linux — Wayland | xdg-shell | Vulkan + wl_shm fallback (`-DWL_EGL_PLATFORM=ON`) | ✅ on KDE Plasma 6+ / GNOME 47+ (Vulkan path) | **Implemented** |

On Wayland, GL contexts run through GLX on XWayland and window surfaces are
presented through a Vulkan swapchain when the driver provides GL/Vulkan
interop. Linear, sRGB and OpenVG surfaces additionally have a software
presentation path over `wl_shm`, and OpenGL ES is delegated to the system
libEGL through libwayland-egl. HDR color spaces need the Vulkan path.

## EGL 1.5 API Coverage

The full EGL 1.5 API surface is implemented in the platform-agnostic core:

- Display management: `eglGetDisplay`, `eglInitialize`, `eglTerminate`,
  `eglGetPlatformDisplay`
- Config selection: `eglGetConfigs`, `eglChooseConfig`, `eglGetConfigAttrib`
- Context management: `eglCreateContext`, `eglDestroyContext`, `eglMakeCurrent`,
  `eglGetCurrentContext`, `eglGetCurrentDisplay`, `eglGetCurrentSurface`,
  `eglQueryContext`
- Surface management: `eglCreateWindowSurface`, `eglCreatePbufferSurface`,
  `eglCreatePixmapSurface`, `eglCreatePbufferFromClientBuffer`,
  `eglCreatePlatformWindowSurface`, `eglCreatePlatformPixmapSurface`,
  `eglDestroySurface`, `eglQuerySurface`, `eglSurfaceAttrib`
- Rendering: `eglSwapBuffers`, `eglSwapInterval`, `eglCopyBuffers`,
  `eglBindTexImage`, `eglReleaseTexImage`, `eglWaitClient`, `eglWaitNative`,
  `eglWaitGL`
- Sync objects (EGL 1.5 / GL_ARB_sync): `eglCreateSync`, `eglDestroySync`,
  `eglClientWaitSync`, `eglWaitSync`, `eglGetSyncAttrib`
- Image objects: `eglCreateImage`, `eglDestroyImage`
- Threading: `eglBindAPI`, `eglQueryAPI`, `eglReleaseThread` (per-thread state
  via `thread_local`)
- Utilities: `eglGetError`, `eglGetProcAddress`, `eglQueryString`

## OpenGL ES support

This library targets **desktop OpenGL** as its primary client API. It can
additionally route `EGL_OPENGL_ES_API` contexts to a pluggable ES backend so
that one and the same `libEGL` produced here serves both GL and ES through
the standard EGL entry points.

The ES backend is intentionally an **option, not a hard dependency** — the
core EGL layer has no link-time requirement on any ES implementation. Each
platform can wire in whichever ES provider is appropriate:

| Platform | Available ES backends | Status |
| --- | --- | --- |
| Windows | [ANGLE](https://github.com/google/angle) (D3D11) | **implemented** |
| Linux — X11 | system `libGLESv2` (Mesa / vendor) | **implemented** |
| Linux — Wayland | system `libGLESv2` (Mesa / vendor) | **implemented** |

A platform with no ES backend simply returns `EGL_BAD_MATCH` from
`eglCreateContext` after `eglBindAPI(EGL_OPENGL_ES_API)`; desktop GL keeps
working unchanged.

### Selecting the client API at runtime

Application code is identical to any standard EGL implementation — there is
no separate library, no separate display, no separate code path:

```cpp
EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
eglInitialize(dpy, nullptr, nullptr);

// Pick one — per thread, before eglCreateContext:
eglBindAPI(EGL_OPENGL_API);     // -> desktop OpenGL
// eglBindAPI(EGL_OPENGL_ES_API); // -> OpenGL ES 2.0 / 3.x via the active ES backend

EGLConfig cfg; EGLint n = 0;
eglChooseConfig(dpy, configAttribs, &cfg, 1, &n);

EGLContext ctx  = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttribs);
EGLSurface surf = eglCreateWindowSurface(dpy, cfg, hwnd, surfAttribs);
eglMakeCurrent(dpy, surf, surf, ctx);
```

`eglBindAPI` is per-thread; the next `eglCreateContext` honors whichever API
was last bound. On Windows the choice is **sticky per `HWND`**
(`SetPixelFormat` is one-shot, and ANGLE owns the `HWND` through D3D11) —
pick GL or ES before creating the first surface for that window, then keep
it. Different threads / windows can use different APIs concurrently.

### Windows ES backend: ANGLE (option)

ANGLE is the currently-implemented ES backend on Windows. It is **not bundled**
— the library loads it at runtime via `LoadLibrary("libEGL.dll" /
"libGLESv2.dll")`, so a Windows build that does not need ES can omit ANGLE
entirely.

Build switches:

- `-DEGL_WIN_ENABLE_ANGLE=ON` (default) — compile the ANGLE routing code. If the
  ANGLE DLLs are not found at runtime, ES context creation fails cleanly and
  desktop GL is unaffected.
- `-DEGL_WIN_ENABLE_ANGLE=OFF` — desktop-GL-only `libEGL`, identical to the
  pre-ANGLE behavior. No vcpkg required.

#### Getting ANGLE via vcpkg (recommended)

This repo ships a `vcpkg.json` manifest that pins a known-good ANGLE.
vcpkg in **manifest mode** fetches it automatically during CMake configure —
there is no global `vcpkg install` step.

Requirements:

- CMake 3.10+, MSVC (Visual Studio 2019 or newer)
- Vulkan SDK
- vcpkg (any recent checkout). If you do not have it:

  ```sh
  git clone https://github.com/microsoft/vcpkg C:\vcpkg
  C:\vcpkg\bootstrap-vcpkg.bat
  ```

  Some Visual Studio installs already ship vcpkg under
  `C:\Program Files\Microsoft Visual Studio\<ver>\Community\VC\vcpkg`.

Configure and build:

```sh
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build .
```

First configure downloads and builds ANGLE into `vcpkg_installed/`
(gitignored); subsequent configures reuse the binary cache. To upgrade
ANGLE later, bump the `builtin-baseline` (or `version>=`) in `vcpkg.json`
and reconfigure.

Other ways to provide ANGLE (e.g. system install, a manual download, or a
different package manager) work as well as long as
`find_package(unofficial-angle CONFIG)` succeeds; vcpkg is just the convenient
default.

What gets built:

- `lib/libEGL.lib` — same static lib as before, with ES routing compiled in.
- `bin/<example>_GL_Windows_WGL_VK.exe` — desktop GL examples (unchanged).
- `bin/green_window_ES_Windows_ANGLE.exe` — ES proof example.
- `bin/libEGL.dll`, `bin/libGLESv2.dll` — ANGLE runtime DLLs copied next to the
  ES executable by a post-build step. Ship these alongside any application that
  uses `EGL_OPENGL_ES_API`.

### Linux ES backend: system libGLESv2 (option)

On Linux/X11 the ES backend loads the **system** EGL and GLES libraries at
runtime via `dlopen` — the same Mesa or vendor driver your application would
normally use directly:

```text
libEGL.so.1   (system — Mesa or vendor)
libGLESv2.so.2
```

No source modifications are required. The library detects the system
libraries at `eglInitialize` time and silently disables the GLES path if
they are absent; desktop GL is unaffected.

Build switches:

- `-DEGL_LINUX_ENABLE_GLES=ON` (default) — compile the system GLES routing code.
  If `libEGL.so.1` / `libGLESv2.so.2` are not present at runtime,
  `eglCreateContext` with `EGL_OPENGL_ES_API` returns `EGL_BAD_MATCH` and the
  desktop GL path continues normally.
- `-DEGL_LINUX_ENABLE_GLES=OFF` — desktop-GL-only build, no GLES code
  compiled.

Prerequisites (Ubuntu / Debian):

```sh
sudo apt install libgles2-mesa-dev   # or equivalent for your distribution
```

The library links `libGLESv2` at build time (for the ES example's `glClear`
calls) and loads `libEGL.so.1` dynamically at runtime.

What gets built:

- `lib/libEGL.a` — same static lib as before, with system GLES routing compiled
  in.
- `bin/<example>_GL_Linux_X11_GLX` — desktop GL examples (unchanged).
- `bin/green_window_ES_Linux_X11_GLES` — ES proof example.

### Linux Wayland ES backend: system libGLESv2 (option)

On Linux/Wayland the ES backend likewise loads the **system** EGL and GLES
libraries at runtime via `dlopen`.

Prerequisites (Ubuntu / Debian):

```sh
sudo apt install libgles2-mesa-dev libwayland-dev wayland-protocols libvulkan-dev
```

Configure and build:

```sh
mkdir build_wl
cd build_wl
cmake .. -DWL_EGL_PLATFORM=ON -DEGL_WAYLAND_ENABLE_GLES=ON
cmake --build .
```

Output suffix:

- `_ES_Linux_Wayland_GLES`

### Why this exists (and why it is optional)

ANGLE alone already covers many ES + HDR scenarios on Windows (HDR10, scRGB,
Display P3). If your application only needs ES with those colorspaces, you can
use ANGLE directly without this library. This library focuses on what ANGLE does
**not** provide: full HDR signaling for desktop GL and Vulkan, including BT.2020
HLG, BT.2020 linear, mastering-display / content-light metadata via Vulkan
`vkSetHdrMetadataEXT` (`VK_EXT_hdr_metadata`: SMPTE2086 primaries + luminance,
CTA861.3 MaxCLL / MaxFALL), and Wayland `wp_color_management_v1`. Bundling ES
routing through the same `libEGL` lets a single application codebase reach both
worlds — but the ES side stays opt-in so projects that only want desktop GL pay
nothing for it.

## HDR Support

The following colorspace extensions are probed at `eglInitialize` time and
advertised only if the driver and display support them:

| Extension | Colorspace | Format |
| --- | --- | --- |
| `EGL_EXT_gl_colorspace_scrgb_linear` | scRGB linear | R16G16B16A16_SFLOAT |
| `EGL_EXT_gl_colorspace_scrgb` | scRGB gamma | R16G16B16A16_SFLOAT |
| `EGL_EXT_gl_colorspace_bt2020_pq` | BT.2020 PQ / HDR10 | A2B10G10R10_UNORM |
| `EGL_EXT_gl_colorspace_bt2020_linear` | BT.2020 linear | R16G16B16A16_SFLOAT |
| `EGL_EXT_gl_colorspace_bt2020_hlg` | BT.2020 HLG | A2B10G10R10_UNORM |
| `EGL_EXT_gl_colorspace_display_p3` | Display P3 (sRGB EOTF) | R8G8B8A8_UNORM |
| `EGL_EXT_gl_colorspace_display_p3_linear` | Display P3 linear | R16G16B16A16_SFLOAT |
| `EGL_EXT_gl_colorspace_display_p3_passthrough` | Display P3 passthrough | R8G8B8A8_UNORM |

`EGL_EXT_gl_colorspace_display_p3` and
`EGL_EXT_gl_colorspace_display_p3_passthrough` share a single Display-P3
capability bit, so they are advertised together. When any HDR/wide-gamut
colorspace is available the library additionally advertises
`EGL_EXT_surface_SMPTE2086_metadata` and `EGL_EXT_surface_CTA861_3_metadata` for
supplying mastering-display and content-light metadata.

Mastering/content-light metadata set via `eglSurfaceAttrib` is forwarded to the
swapchain with Vulkan `vkSetHdrMetadataEXT` (`VK_EXT_hdr_metadata`) on both
Windows and Linux. It is only submitted for the colorspaces that actually
consume it — HDR10 PQ (`bt2020_pq`) and HLG (`bt2020_hlg`) — and only when the
application has supplied values.

## Building

All build variants share one source tree. The CMake configuration sets a
per-variant `EGL_BACKEND_SUFFIX` that's appended to every executable's filename,
e.g. `linear` → `linear_GL_Windows_WGL_VK.exe`, `linear_GL_Linux_Wayland_VK`,
etc. When `EGL_WIN_ENABLE_ANGLE` is on, the Windows ES variant uses suffix
`_ES_Windows_ANGLE`. When `EGL_LINUX_ENABLE_GLES` is on, the Linux ES variant
uses suffix `_ES_Linux_X11_GLES`.

### Windows — WGL + Vulkan HDR (implemented)

Requirements: CMake 3.10+, MSVC (Visual Studio 2019+), Vulkan SDK.

```sh
mkdir build
cd build
cmake ..
cmake --build .
```

Outputs:

- `lib/libEGL.lib`
- `bin/linear_GL_Windows_WGL_VK.exe`, `bin/bt2020_pq_GL_Windows_WGL_VK.exe`,
  etc. (all 10 colorspace examples)

If CMake cannot find the Vulkan SDK, ensure `VULKAN_SDK` is set in your
environment.

### Linux — X11 / GLX (implemented, SDR only)

Requirements: CMake 3.10+, GCC/Clang, X11 and OpenGL development headers.

On Ubuntu / Debian / WSL2 Ubuntu:

```sh
sudo apt install build-essential cmake ninja-build pkg-config \
    libx11-dev libxext-dev libgl-dev libglu1-mesa-dev
```

```sh
mkdir build_x11
cd build_x11
cmake .. -DUSE_X11=ON
cmake --build .
```

Outputs:

- `lib/libEGL.a`
- `bin/linear_GL_Linux_X11_GLX`, `bin/srgb_GL_Linux_X11_GLX`, etc. (all 10
  colorspace examples)

X11 has no HDR signaling protocol, so HDR examples will exit gracefully with
"colorspace not supported" — only `linear` and `srgb` produce visible output.

### Linux — X11 / GLX + Vulkan HDR (implemented, driver-dependent)

Same prerequisites as X11 plus the Vulkan SDK / loader:

```sh
sudo apt install libvulkan-dev vulkan-tools
```

```sh
mkdir build_x11vk
cd build_x11vk
cmake .. -DUSE_X11=ON -DLINUX_VK=ON
cmake --build .
```

Outputs:

- `bin/linear_GL_Linux_X11_GLX_VK`, `bin/bt2020_pq_GL_Linux_X11_GLX_VK`, etc.

HDR availability depends on whether the running compositor and driver expose HDR
colorspaces on a Vulkan X11 surface. Most X11 compositors do not, so this
variant is mainly useful for direct rendering or future-proofing.

### Linux — Wayland + Vulkan HDR (implemented, recommended for HDR on Linux)

Requirements: same as above, plus Wayland development files:

```sh
sudo apt install libwayland-dev wayland-protocols pkg-config libvulkan-dev
```

```sh
mkdir build_wl
cd build_wl
cmake .. -DWL_EGL_PLATFORM=ON
cmake --build .
```

Outputs:

- `bin/linear_GL_Linux_Wayland_VK`, `bin/bt2020_pq_GL_Linux_Wayland_VK`, etc.

For working HDR on Linux, this variant requires:

- A native Linux session (not WSL2 — WSLg cannot pass HDR signals through)
- A Wayland HDR-capable compositor (KDE Plasma 6.1+ or GNOME 47+)
- An HDR-capable GPU driver (NVIDIA 555+, Mesa RADV recent, etc.)
- HDR enabled in the compositor's display settings

## Examples

| Executable (base name) | Colorspace | HDR-class |
| --- | --- | --- |
| `srgb` | sRGB | SDR |
| `linear` | Linear | SDR |
| `scrgb_linear` | scRGB linear (fp16) | HDR |
| `scrgb` | scRGB gamma (fp16) | HDR |
| `bt2020_pq` | BT.2020 PQ / HDR10 | HDR |
| `bt2020_linear` | BT.2020 linear | HDR |
| `bt2020_hlg` | BT.2020 HLG | HDR |
| `display_p3` | Display P3 (sRGB EOTF) | Wide-gamut SDR |
| `display_p3_linear` | Display P3 linear | Wide-gamut SDR |
| `display_p3_passthrough` | Display P3 passthrough | Wide-gamut SDR |

The full filename includes the build variant suffix, e.g.
`bt2020_pq_GL_Windows_WGL_VK.exe`, `bt2020_pq_GL_Linux_Wayland_VK`. The window
title likewise shows the variant in `[OS/Windowing/Backend]` form (e.g. `EGL
BT.2020 PQ [Linux/Wayland/VK]`).

Each example checks at runtime whether its colorspace is supported on the
current platform/driver and exits with a message if not, so cross-platform
testing never crashes.

## Architecture

```text
egl.c                     Public C API (thin shims, no logic)
  └── egl_globals.cpp      Global + per-thread storage, init/terminate lifecycle
  └── egl_config.cpp       Config selection and queries
  └── egl_display.cpp      Display management and extension string
  └── egl_context.cpp      Context create/destroy/makecurrent
  └── egl_surface.cpp      Surface create/destroy/query
  └── egl_sync.cpp         Sync objects (EGL 1.5)
  └── egl_image.cpp        Image objects (EGL 1.5)
  └── egl_api.cpp          Swap, bind, wait, getProcAddress

  Internal headers:
  └── egl_internal.h       Backend interface declarations (implemented by platform backends)
  └── egl_common.h         Shared internal types and helpers
  └── egl_windows_vk.h     Windows Vulkan HDR — internal declarations for egl_windows_vk.cpp
  └── egl_linux_vk.h       Linux Vulkan HDR — internal declarations for egl_linux_vk.cpp
  └── egl_windows_angle.h  Windows ANGLE ES backend — runtime loader declarations
  └── egl_linux_gles.h     Linux system GLES backend — runtime loader declarations
  └── wglext.h             WGL extension prototypes (Windows)

  Platform backends (implement the functions declared in egl_internal.h):
  └── egl_windows.cpp      Windows — WGL
  └── egl_windows_angle.cpp Windows — ANGLE ES backend (dlopen-style via LoadLibrary)
  └── egl_windows_vk.cpp   Windows — Vulkan HDR presentation
  └── egl_x11_glx.cpp      Linux / Unix — X11 + GLX (+ optional system GLES routing)
  └── egl_linux_gles.cpp   Linux — system libEGL/libGLESv2 ES backend (X11 and Wayland, dlopen)
  └── egl_wayland.cpp      Linux / Unix — Wayland + xdg-shell (paired with egl_linux_vk.cpp)
  └── egl_linux_vk.cpp     Linux — Vulkan HDR presentation (shared by X11+VK and Wayland)
  └── egl_<platform>.cpp   Future backends
```

## Changelog

17.10.2026 - The analysis lanes moved into CI and the first Linux
static-analyzer pass over the full source set paid for itself immediately.
EGLApp, the shared example application struct, now initializes every member
in-class: its values are the EGL sentinels no matter how the struct is
allocated, which is what the cleanup ladder on the partial-failure teardown
paths always assumed. The chooseConfig stack storage is zero-filled for the
same reason. The static archive lands per build tree under lib/ instead of a
shared lib/libEGL - the sibling build trees carry different feature sets
(WGL+VK, ANGLE, sanitizer, wayland) and the ninja up-to-date check linked
examples against a foreign tree's archive. The clang-tidy runner gained
--exclude-dir for vendored scopes that carry their own disabling
.clang-tidy, and the CI analysis job now runs cppcheck and clang-tidy over
both Linux lanes plus the lint matrix on every push. v1.0.19.

15.10.2026 - Quality tooling brought to the lint-matrix baseline: yamllint
compacted the release matrix entries and pinned workflow files to LF through a
.gitattributes rule, Ruff cleaned the clang-tidy runner (unused import,
explicit subprocess check, f-strings) which also gained --allow-no-checks so a
vendored subtree can carry a documented opt-out .clang-tidy, markdown received
fence languages, atx title styles and an 80-column prose wrap across all
documentation, and the sanitizer lane (ASan/UBSan build plus instrumented
example runs) came back clean. All local analysis gates run at zero findings
and the dEQP results are unchanged. v1.0.18.

14.10.2026 - Completed the OpenGL ES delegation and took dEQP-EGL conformance
from 28 failures to the two documented driver limits. ES contexts previously ran
in the delegated system stack but their pbuffer surfaces stayed on GLX, so a
delegated context could not bind them; window and pbuffer surfaces of ES
contexts now delegate too, behind a requested-format to system-config resolution
(cached, queried once per format) that feeds BOTH the surface and the context -
EGL requires them from the same config, and the first cut gave the surface a
format-matched config while the context stayed on a fixed one, which broke
render, color_clears and wide_color exactly that way. Two wrong token constants
hid behind the fixed config: the stencil size went out as EGL_CONFIG_CAVEAT
(0x3027 instead of EGL_STENCIL_SIZE 0x3026), so every format query matched
nothing, and the colorspace values were 0x308E/0x308F instead of
EGL_GL_COLORSPACE_SRGB/LINEAR 0x3089/0x308A, so delegated window surfaces never
received their sRGB attribute; all SYSGL_ token constants are audited against
the Khronos headers now. ANGLE reached image parity (create/destroy/target entry
points forwarding into ANGLE's GLES library) and the GLVND entry point
resolution from v1.0.16 carries both stacks. Conformance fixes, all from the EGL
1.5 error contract: eglQuerySurface leaves value untouched for
EGL_LARGEST_PBUFFER, EGL_TEXTURE_FORMAT, EGL_TEXTURE_TARGET and the mipmap
attributes on non-pbuffer surfaces (section 3.5.6); eglSwapBuffers on a current
pbuffer or pixmap is a successful no-op instead of EGL_BAD_SURFACE, and a window
swap adopts the native window's size through a per-backend geometry hook
(section 3.10.1.1); eglSwapInterval validates the display before the context
requirement; eglCreatePbufferSurface rejects negative dimensions with
EGL_BAD_PARAMETER and mismatched texture format/target pairs with EGL_BAD_MATCH
(section 3.5.1); eglCreatePbufferFromClientBuffer validates display, buftype and
config and reports EGL_BAD_PARAMETER for client buffers (it was an unconditional
EGL_BAD_ACCESS stub that never looked at the display);
eglGetConfigs/eglChooseConfig accept a negative config_size (no such error
exists in section 3.4.1); and the GLX enumeration fills
EGL_BIND_TO_TEXTURE_RGB/RGBA from GLX_EXT_texture_from_pixmap instead of leaking
the match-template's EGL_DONT_CARE into queries. The damage extensions are
implemented behind honest native ages: EGL_EXT_buffer_age,
EGL_KHR_swap_buffers_with_damage and EGL_KHR_partial_update with the age
answered by the native stack wherever one exists (the delegated system EGL
answers directly, GLX through GLX_EXT_buffer_age) - only the driver knows
whether it presents by exchange or copy, and a fixed local model failed 81
buffer_age cases on this stack while the driver copies; without a native answer
the core reports the conservative model. The swap-with-damage rectangles are a
compositor hint the extension explicitly allows discarding, and the
partial-update state machine enforces the spec's BAD_MATCH/BAD_ACCESS rules
around the per-frame flags. EGL_SWAP_BEHAVIOR_PRESERVED became a real capability
on every surface type instead of an unreachable bit: delegated surfaces forward
the attribute into their owning stack, GLX and WGL windows keep contents through
a copy-back around the swap in the new egl_gl_preserve module (RGBA/FLOAT
round-trip of every precision, client state saved with push/pop attrib pairs),
and pbuffers, pixmaps, the Vulkan present paths and the Wayland shm presenter
never mutate the back buffer while posting. EGL_KHR_surfaceless_context binds a
context with EGL_NO_SURFACE for both draw and read: (context, no-surface) list
nodes take their native context from the context's own config, GLX binds both
drawables to None, the system GLES and ANGLE stacks take EGL_NO_SURFACE
directly, and WGL - which binds contexts to an HDC - uses a hidden
per-pixel-format window DC. EGL_KHR_reusable_sync adds the semaphore-like status
object: eglSignalSyncKHR transitions and wakes every waiter, eglClientWaitSync
blocks on the shared state outside the display mutex so a concurrent destroy
cannot free the mutex a waiter sleeps on (destruction wakes waits as if
signaled), and the core eglCreateSync accepts the extension type while it is
advertised. The surface HDR metadata extensions
(EGL_EXT_surface_SMPTE2086_metadata, EGL_EXT_surface_CTA861_3_metadata) are
advertised for the field set/query paths the core already carried;
EGL_EXT_hdr_metadata stays unadvertised because this library has no
eglSetHdrMetadataKHR. Verified with dEQP-EGL linking this library statically,
proven by the version string inside the test binary, against the v1.0.14
baseline: 930 Pass / 28 Fail (plus a SIGSEGV in the image group) to 2307 Pass /
2 Fail with zero crashes - query_surface, resize, negative_api and query_config
all reach zero Fail, sharing gains 947 cases (1272/1272), image completes all
226, preserve_swap 30/30, buffer_age 168/168, swap_buffers_with_damage 54/54,
partial_update 29/29, negative_partial_update 7/7, reusable_sync 23/23,
surfaceless_context 10 Pass, hdr_metadata 2/2, fence_sync 28/28. The two
remaining failures are the documented 10-10-10-2 pbuffer readPixels limitation
of this Mesa stack (GLX had such a config, the system EGL does not), and every
remaining NotSupported case is honest: Android-gralloc mappings that do not
apply, config variants this implementation does not claim, combos the raw driver
does not expose (probed: zero matching GLX fbconfigs), and the upstream CTS X11
factory's missing readScreenPixels. Windows WGL and ANGLE builds clean,
clang-tidy 0/0 across 28 translation units, cppcheck clean on both lanes, 7/7
Windows examples and both X11 examples (GLX and delegated ES) render green.
v1.0.17.

13.10.2026 - Fixed the EGL entry point contract for images and syncs.
eglGetProcAddress deferred unsupported `egl*` names to the client loader, which
returned another EGL implementation's function pointer - or NULL before any
display exists, exactly when clients resolve their entry points - so dEQP-EGL
died with a SIGSEGV at case 14 of the 226-case image group. Three rules hold
now: every core function and its pre-1.5 extension alias resolves to this
library, unknown `egl*` names stay NULL, and the GL_OES_EGL_image consumption
entry points are implemented here because they carry this library's EGLImage
handles. The KHR aliases of the EGL 1.5 core functionality are implemented as
the thin aliases they are: eglCreateImageKHR/eglDestroyImageKHR
(EGL_KHR_image_base) and eglCreateSyncKHR, eglDestroySyncKHR,
eglClientWaitSyncKHR, eglGetSyncAttribKHR, eglWaitSyncKHR (EGL_KHR_fence_sync /
EGL_KHR_wait_sync), with eglCreateSyncKHR reporting EGL_BAD_ATTRIBUTE for an
unsupported type as its extension specifies - the core 1.5 function keeps
EGL_BAD_PARAMETER, into which the error was deliberately changed (spec change
log, Bug 11963). The extension string now communicates exactly the core-promoted
functionality this implementation carries: fence and wait syncs,
EGL_KHR_image_base and the GL texture and renderbuffer image sources. Contexts
of the delegated GLES stack create their images in that stack (its consumption
entry points dereference the handle with their own object model), and the image
consumption calls are forwarded there when both the current context and the
image are delegated; GLX-backed contexts - including ES contexts on pbuffer
surfaces - keep bookkeeping images, as GLX has no EGLImage interop, and
consumption is accepted without effect for them. eglClientWaitSync is valid
without a current context (section 3.8.1.3): the fence records the binding it
was issued into, and a context-free wait borrows that binding through the
platform `__makeCurrent` hook, releasing it afterwards. Verified with dEQP-EGL
on X11: the image group completes for the first time (226/226 cases, no crash),
fence_sync goes from excluded to 28/28 Pass, and negative_api, swap_buffers,
preserve_swap, query_config and query_surface are byte-identical to the v1.0.15
baseline; Windows and WSL build clean with nothing from cppcheck and clang-tidy,
and the windowed Windows examples render green (6 pass, 6 HDR color spaces
skipped as unsupported on the current display). The 62 remaining image failures
render through the image on pbuffer contexts and need ES pbuffer contexts
delegated - recorded as follow-up. v1.0.16.

11.10.2026 - Wayland backend brought up to the X11 one: window surfaces render
and present, and OpenVG and OpenGL ES work. Presentation still goes through the
Vulkan swapchain when the driver provides GL/Vulkan interop; linear, sRGB and
OpenVG surfaces additionally present through a software `wl_shm` path
(GLXPbuffer readback or the VG pixel buffer copied into XRGB8888 slots,
double-buffered on a private event queue, resized on swap per section 3.10.1.1),
so a driver without the interop extensions - Mesa's lavapipe, for instance - no
longer fails every `eglCreateWindowSurface` with `EGL_BAD_ALLOC`. HDR color
spaces are carried by the Vulkan swapchain only, as before. The ES delegation no
longer hands our native window struct to the system libEGL - that library
expects the `wl_egl_window` of libwayland-egl, whose struct layout is private
(wayland-egl-core.h only declares create/destroy/resize), and rejected the
hand-rolled one with `EGL_BAD_ALLOC`. The native window is now mirrored through
those libwayland-egl entry points and kept in sync on resize. Error contract:
`eglSwapBuffers` sets `EGL_BAD_NATIVE_WINDOW` when a post fails (section 3.10.4)
on every backend - it returned `EGL_FALSE` without an error code before, leaving
`eglGetError()` at `EGL_SUCCESS`. CMake fails at configure time when the
wayland-protocols `xdg-shell.xml` is missing instead of a late, opaque
`wayland-scanner` error. Analysis: `__makeCurrent` guards a null surface
container (cppcheck), the examples' `AppBuilder` is non-copyable. Verified by
pixel inspection of the windowed examples on WSLg (GL, ES and OpenVG all render
green in 800x600 compositor windows) and on Windows (6 pass, 6 HDR color spaces
skipped as unsupported on the current display); cppcheck clean on both
platforms, clang-tidy 0 errors; the swap-related dEQP-EGL groups match the
v1.0.14 results exactly. v1.0.15.

11.10.2026 - Fixed the v1.0.13 window-surface regression: default window
surfaces are back-buffered again on every backend. v1.0.13 let the WGL window
template take `WGL_DOUBLE_BUFFER_ARB` from the spec-sorted first config, which
is single-buffered, so `eglSwapBuffers` had nothing to present - on Windows the
window stayed white while running and only turned green at close, when the GL
queue flushed at context release. Per EGL 1.5 section 3.5.1 (`EGL_RENDER_BUFFER`
defaults to `EGL_BACK_BUFFER`) the Windows window template now requests a
double-buffered pixel format by default, with an explicit `EGL_RENDER_BUFFER`
attribute still overriding it; on GLX and Wayland, config enumeration grants
`EGL_WINDOW_BIT` only to double-buffered fbconfigs (`GLX_DOUBLEBUFFER`),
matching Mesa - GLX has no per-surface override, so a single-buffered fbconfig
cannot back a window surface at all. The examples now guard the invariant: a
default window surface must report `EGL_BACK_BUFFER` from `eglQuerySurface`
(section 3.5.6). Verified by pixel inspection of the windowed examples on
Windows and by an X11 probe on Linux (double-buffered config, surface and
context report `EGL_BACK_BUFFER`). Analysis: fixed the two clang-tidy findings
already red at v1.0.13 - the duplicated HDR colorspace branch in the Windows
pbuffer path (no behaviour change) and the unbounded `strcat` building
`EGL_CLIENT_APIS`, now a bounded `snprintf` append with byte-identical output.
dEQP-EGL correction: the "221 pass / 0 fail / 974 not-supported" figure quoted
for v1.0.13 came from a truncated run - it executed 1196 of the 3920
`dEQP-EGL.functional` cases and stopped at the first harness crash, so `render`,
`sharing`, `query_*`, `swap_buffers` and more never ran. The suite is now run
group by group: 3690 of 3920 cases executed (the 226-case `image` group stops
early on a pre-existing crash, and 18 `fence_sync` cases are excluded because
dEQP's `SyncTest::deinit` unconditionally calls `eglDestroySyncKHR`, which this
library does not provide - EGL 1.5 core sync only, no `EGL_KHR_fence_sync`).
Full result: 930 pass / 2730 not-supported / 28 fail / 1 crash / 1 quality
warning, and a per-case A/B comparison against the previous release shows 0
differences - the 28 failures and the crash are pre-existing conformance gaps,
recorded as follow-up work. The Wayland backend is build-verified only in this
release; its pure-Wayland runtime path is known broken and is separate follow-up
work. v1.0.14.

11.10.2026 - dEQP-EGL conformance program: drove the full VK-GL-CTS
`dEQP-EGL.functional` suite against this library (X11/GLX, Mesa llvmpipe) and
fixed every failure - **221 pass / 0 fail / 974 not-supported** (from 5 pass /
61 fail at the first run). Fixes, all verified against the EGL 1.5 specification
text: self-deadlock in shared-library builds; ES renderable-type
over-advertising; `eglMakeCurrent` / platform-window error codes and handle
dereference; `eglChooseConfig` DONT_CARE handling, double-buffer filter,
`EGL_NATIVE_VISUAL_TYPE`/`EGL_LEVEL` criteria and duplicate-attribute
acceptance; `EGL_EXT_client_extensions` moved to the client-only string;
fence-sync error code; pbuffer creation for non-visual fbconfigs; sRGB-capable
GLX fbconfigs filtered from config enumeration (they gamma-encode
unconditionally, which made every `color_clears` rgba8888 comparison fail on the
affected configs); the EGL 1.5 core surface color space implemented on both
backends (`EGL_GL_COLORSPACE_SRGB` now renders sRGB via `GL_FRAMEBUFFER_SRGB`
set at bind time, pixmaps accept the attribute, HDR colorspaces on pbuffers are
rejected) - on Windows the sRGB pixel-format attribute is only sent to the
chooser for explicit sRGB surfaces, because WGL sRGB-capable formats render
linear until the enable is set (filtering them, as GLX requires, gutted the
config list to the generic GDI formats on NVIDIA and broke every linear window
surface); GL 3.1 context requests sent as 3.2 core per the section 3.7.1.1
version rules. Also added `EGL_BUILD_SHARED`, and the version string tracks
releases again (it had been stuck at 1.0.8). v1.0.13.

11.10.2026 - OpenVG client API seam behind `EGL_ENABLE_OPENVG` (opt-in, default
OFF), letting an OpenVG library provide the VG client API without affecting
OpenGL builds. v1.0.12.

10.10.2026 - Bug fixes and refactoring, no public API changes. v1.0.11.

07.10.2026 - Added CI build workflows covering Windows (WGL + Vulkan HDR) and
Linux (X11/GLX, X11 + Vulkan HDR, Wayland); every push and pull request is
build-verified, and tagged releases attach the prebuilt static library with
headers to the GitHub release. No library changes. v1.0.10.

24.09.2026 - Correctness and packaging pass. Moved the first-party
`eglctxinternals.h` out of the public `include/` tree into `src/`, so `include/`
now holds only the Khronos API headers. Replaced a `memcmp` over the padded
`VkHdrMetadataEXT` with a field-wise comparison (C does not require struct
assignment to preserve padding, so the HDR metadata dirty-check could re-send
the metadata on every present). Added explicit `default:` cases to the attribute
and config dispatch switches. Replaced `#pragma once` with include guards. Added
the `EGL_BUILD_EXAMPLES` option so consumers can vendor the library without
building the examples. Documented the correct
`EGL_EXT_gl_colorspace_display_p3_passthrough` extension name. Declared the
project version, and added the `cppcheck` and `clang-tidy` analysis lanes and
`AGENTS.md`. v1.0.9.

01.08.2026 - Comprehensive correctness, memory-safety and thread-safety pass.
Fixed eglQueryContext out-of-bounds read; eglChooseConfig num_config reporting;
per-thread current-binding tracking with reference counting; eglTerminate
resource cleanup; cleanup loop skipping list-head nodes; eglGetDisplay
atomicity; sRGB as per-config capability; Vulkan swapchain recreate
double-free/OOB; per-frame/per-image semaphore rings; required vs optional
device extensions; compositeAlpha negotiation; SetPixelFormat-once handling; WGL
attribute green/blue swap; X11 teardown ordering; Wayland private event queue;
and numerous spec-conformance error-code fixes. Verified on Windows (MSVC) and
Linux (GCC, X11 + Wayland). v1.0.8.

02.06.2026 - Correctness pass, including fixes and documentation alignment.
v1.0.7.

27.05.2026 - Fixed ReleaseDC resource leak on Windows. Fixed memory leak and
uninitialized fence handles on Vulkan swapchain resize. Fixed eglWaitGL binding
wrong API. Fixed duplicate CMake option blocks. v1.0.6.

14.05.2026 - Almost full EGL implementation for Linux and Windows. v1.0.5.

03.05.2026 - Fixed OpenGL and Vulkan coordinate. v1.0.4.

03.05.2026 - Added OpenGL ES support via ANGLE on Windows. Documentation fixes.
v1.0.3.

03.05.2026 - Added Linux Wayland + Vulkan HDR backend. v1.0.2.

19.04.2026 - Added Linux X11/GLX backend. v1.0.1.

19.04.2026 - Major refactoring using AI v1.0.0.

29.01.2015 - Updated to GLEW 1.12.0. v0.3.3.

25.01.2015 - Fixed initialization bug on Windows. v0.3.3.

20.01.2015 - Added GLX version check. Fixed window creation bug on X11. v0.3.2.

05.12.2014 - Removed duplicate code. v0.3.1.

04.12.2014 - Working X11 version. v0.3.0.

28.11.2014 - Continued X11 implementation. v0.2.3.

22.11.2014 - X11 compiling but not complete. v0.2.2.

18.11.2014 - Added X11 build configuration. v0.2.1.

17.11.2014 - First public release. v0.2.
