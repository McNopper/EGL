# EGL 1.5 Implementation

A portable, spec-compliant implementation of [EGL 1.5](https://www.khronos.org/registry/egl/) built around
a platform-agnostic core and pluggable OS/rendering backends. The library provides the standard EGL
surface creation, context management, and synchronization API across operating systems, so that
applications can share a single EGL-based rendering path regardless of the underlying platform.

The Khronos headers bundled with the library are the official, unmodified ones from the
[EGL Registry](https://github.com/KhronosGroup/EGL-Registry) (commit e80a2e0050, 2026-03-19).

## Purpose

The primary goal is **portable HDR rendering**: write your application against EGL 1.5 once,
and let the library translate `EGL_GL_COLORSPACE_BT2020_PQ_EXT`, `EGL_GL_COLORSPACE_SCRGB_LINEAR_EXT`,
and the other HDR colorspaces into whatever underlying stack the OS provides.

The same EGL call routes through completely different layers per platform:

| Layer | Windows | Linux Wayland |
|---|---|---|
| Window system | Win32 / WGL | Wayland + xdg-shell |
| HDR signaling | DXGI swap chain HDR metadata | `wp_color_management_v1` protocol |
| Vulkan WSI | `VK_KHR_win32_surface` | `VK_KHR_wayland_surface` |
| HDR metadata API | `IDXGISwapChain4::SetHDRMetaData` | `vkSetHdrMetadataEXT` |
| GPU driver | NVIDIA / AMD / Intel Windows | NVIDIA 555+ / Mesa / RADV Linux |

Practical workflow this enables:

1. **Develop on Windows** — fast iteration, immediate HDR validation on a Windows HDR display
2. **Validate on Linux** — boot Fedora KDE (live USB or dual-boot), build the same source tree
   with `-DWL_EGL_PLATFORM=ON`, run the same example binaries, compare results
3. If colors differ, the bug is platform-specific (compositor, driver, or platform glue) — not
   the application

The naming convention encodes the variant in the executable filename and window title
(`bt2020_pq_Windows_WGL_VK.exe` vs `bt2020_pq_Linux_Wayland_VK`) so that screenshot comparisons
across platforms remain unambiguous.


## Platform and Backend Support

| Operating System | Windowing | Backend | HDR | Status |
|---|---|---|---|---|
| Windows | WGL | Vulkan | ✅ | **Implemented** |
| Linux — X11 | GLX | — | ❌ (X11 has no HDR signaling) | **Implemented** |
| Linux — X11 + Vulkan | GLX | Vulkan (`-DLINUX_VK=ON`) | ⚠️ Driver/compositor dependent | **Implemented** |
| Linux — Wayland | xdg-shell | Vulkan (`-DWL_EGL_PLATFORM=ON`) | ✅ on KDE Plasma 6+ / GNOME 47+ | **Implemented** |

## EGL 1.5 API Coverage

The full EGL 1.5 API surface is implemented in the platform-agnostic core:

- Display management: `eglGetDisplay`, `eglInitialize`, `eglTerminate`, `eglGetPlatformDisplay`
- Config selection: `eglGetConfigs`, `eglChooseConfig`, `eglGetConfigAttrib`
- Context management: `eglCreateContext`, `eglDestroyContext`, `eglMakeCurrent`,
  `eglGetCurrentContext`, `eglGetCurrentDisplay`, `eglGetCurrentSurface`, `eglQueryContext`
- Surface management: `eglCreateWindowSurface`, `eglCreatePbufferSurface`,
  `eglCreatePixmapSurface`, `eglCreatePlatformWindowSurface`, `eglCreatePlatformPixmapSurface`,
  `eglDestroySurface`, `eglQuerySurface`, `eglSurfaceAttrib`
- Rendering: `eglSwapBuffers`, `eglSwapInterval`, `eglCopyBuffers`,
  `eglBindTexImage`, `eglReleaseTexImage`, `eglWaitClient`, `eglWaitNative`
- Sync objects (EGL 1.5 / GL_ARB_sync): `eglCreateSync`, `eglDestroySync`,
  `eglClientWaitSync`, `eglWaitSync`, `eglGetSyncAttrib`, `eglSignalSync`
- Image objects: `eglCreateImage`, `eglDestroyImage`
- Threading: `eglBindAPI`, `eglQueryAPI`, `eglReleaseThread` (per-thread state via `thread_local`)
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
|---|---|---|
| Windows | [ANGLE](https://github.com/google/angle) (D3D11) | implemented |
| Linux — X11 | system `libGLESv2` (Mesa / vendor) | **implemented** |
| Linux — Wayland | system `libGLESv2` (Mesa / vendor) | not yet wired |

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

ANGLE is the currently-implemented ES backend on Windows. It is **not bundled** —
the library loads it at runtime via `LoadLibrary("libEGL.dll" / "libGLESv2.dll")`,
so a Windows build that does not need ES can omit ANGLE entirely.

Build switches:

- `-DEGL_WIN_ENABLE_ANGLE=ON` (default) — compile the ANGLE routing code.
  If the ANGLE DLLs are not found at runtime, ES context creation fails
  cleanly and desktop GL is unaffected.
- `-DEGL_WIN_ENABLE_ANGLE=OFF` — desktop-GL-only `libEGL`, identical to the
  pre-ANGLE behavior. No vcpkg required.

#### Getting ANGLE via vcpkg (recommended)

This repo ships a `vcpkg.json` manifest that pins a known-good ANGLE.
vcpkg in **manifest mode** fetches it automatically during CMake configure —
there is no global `vcpkg install` step.

Requirements:

- CMake 3.15+, MSVC (Visual Studio 2019 or newer)
- Vulkan SDK
- vcpkg (any recent checkout). If you do not have it:
  ```
  git clone https://github.com/microsoft/vcpkg C:\vcpkg
  C:\vcpkg\bootstrap-vcpkg.bat
  ```
  Some Visual Studio installs already ship vcpkg under
  `C:\Program Files\Microsoft Visual Studio\<ver>\Community\VC\vcpkg`.

Configure and build:

```
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
different package manager) work as well as long as `find_package(unofficial-angle CONFIG)`
succeeds; vcpkg is just the convenient default.

What gets built:

- `lib/libEGL.lib` — same static lib as before, with ES routing compiled in.
- `bin/<example>_GL_Windows_WGL_VK.exe` — desktop GL examples (unchanged).
- `bin/green_window_ES_Windows_ANGLE.exe` — ES proof example.
- `bin/libEGL.dll`, `bin/libGLESv2.dll` — ANGLE runtime DLLs copied next to
  the ES executable by a post-build step. Ship these alongside any
  application that uses `EGL_OPENGL_ES_API`.

### Linux ES backend: system libGLESv2 (option)

On Linux/X11 the ES backend loads the **system** EGL and GLES libraries at
runtime via `dlopen` — the same Mesa or vendor driver your application would
normally use directly:

```
libEGL.so.1   (system — Mesa or vendor)
libGLESv2.so.2
```

No source modifications are required. The library detects the system
libraries at `eglInitialize` time and silently disables the GLES path if
they are absent; desktop GL is unaffected.

Build switches:

- `-DEGL_LINUX_ENABLE_GLES=ON` (default) — compile the system GLES routing
  code. If `libEGL.so.1` / `libGLESv2.so.2` are not present at runtime,
  `eglCreateContext` with `EGL_OPENGL_ES_API` returns `EGL_BAD_MATCH` and
  the desktop GL path continues normally.
- `-DEGL_LINUX_ENABLE_GLES=OFF` — desktop-GL-only build, no GLES code
  compiled.

Prerequisites (Ubuntu / Debian):

```
sudo apt install libgles2-mesa-dev   # or equivalent for your distribution
```

The library links `libGLESv2` at build time (for the ES example's `glClear`
calls) and loads `libEGL.so.1` dynamically at runtime.

What gets built:

- `lib/libEGL.a` — same static lib as before, with system GLES routing
  compiled in.
- `bin/<example>_GL_Linux_X11_GLX` — desktop GL examples (unchanged).
- `bin/green_window_ES_Linux_X11_GLES` — ES proof example.

### Why this exists (and why it is optional)

ANGLE alone already covers many ES + HDR scenarios on Windows (HDR10, scRGB,
Display P3). If your application only needs ES with those colorspaces, you
can use ANGLE directly without this library. This library focuses on what
ANGLE does **not** provide: full HDR signaling for desktop GL and Vulkan,
including BT.2020 HLG, BT.2020 linear, `IDXGISwapChain4::SetHDRMetaData`
(mastering display, MaxCLL / MaxFALL), and Wayland `wp_color_management_v1`.
Bundling ES routing through the same `libEGL` lets a single application
codebase reach both worlds — but the ES side stays opt-in so projects that
only want desktop GL pay nothing for it.


## HDR Support

The following colorspace extensions are probed at `eglInitialize` time and advertised only if the driver and display support them:

| Extension | Colorspace | Format |
|---|---|---|
| `EGL_EXT_gl_colorspace_scrgb_linear` | scRGB linear | R16G16B16A16_SFLOAT |
| `EGL_EXT_gl_colorspace_scrgb` | scRGB gamma | R16G16B16A16_SFLOAT |
| `EGL_EXT_gl_colorspace_bt2020_pq` | BT.2020 PQ / HDR10 | A2B10G10R10_UNORM |
| `EGL_EXT_gl_colorspace_bt2020_linear` | BT.2020 linear | R16G16B16A16_SFLOAT |
| `EGL_EXT_gl_colorspace_bt2020_hlg` | BT.2020 HLG | A2B10G10R10_UNORM |
| `EGL_EXT_gl_colorspace_display_p3` | Display P3 (sRGB EOTF) | R8G8B8A8_UNORM |
| `EGL_EXT_gl_colorspace_display_p3_linear` | Display P3 linear | R16G16B16A16_SFLOAT |
| `EGL_EXT_gl_colorspace_p3_passthrough` | Display P3 passthrough | R8G8B8A8_UNORM |


## Building

All build variants share one source tree. The CMake configuration sets a per-variant
`EGL_BACKEND_SUFFIX` that's appended to every executable's filename, e.g. `linear` →
`linear_GL_Windows_WGL_VK.exe`, `linear_GL_Linux_Wayland_VK`, etc. When
`EGL_WIN_ENABLE_ANGLE` is on, the Windows ES variant uses suffix `_ES_Windows_ANGLE`.
When `EGL_LINUX_ENABLE_GLES` is on, the Linux ES variant uses suffix `_ES_Linux_X11_GLES`.

### Windows — WGL + Vulkan HDR (implemented)

Requirements: CMake 3.10+, MSVC (Visual Studio 2019+), Vulkan SDK.

```
mkdir build
cd build
cmake ..
cmake --build .
```

Outputs:
- `lib/libEGL.lib`
- `bin/linear_Windows_WGL_VK.exe`, `bin/bt2020_pq_Windows_WGL_VK.exe`, etc. (all 10 examples)

If CMake cannot find the Vulkan SDK, ensure `VULKAN_SDK` is set in your environment.

### Linux — X11 / GLX (implemented, SDR only)

Requirements: CMake 3.10+, GCC/Clang, X11 and OpenGL development headers.

On Ubuntu / Debian / WSL2 Ubuntu:

```
sudo apt install build-essential cmake ninja-build pkg-config \
    libx11-dev libxext-dev libgl-dev libglu1-mesa-dev
```

```
mkdir build_x11
cd build_x11
cmake .. -DUSE_X11=ON
cmake --build .
```

Outputs:
- `lib/libEGL.a`
- `bin/linear_Linux_X11_GLX`, `bin/srgb_Linux_X11_GLX`, etc. (all 10 examples)

X11 has no HDR signaling protocol, so HDR examples will exit gracefully with
"colorspace not supported" — only `linear` and `srgb` produce visible output.

### Linux — X11 / GLX + Vulkan HDR (implemented, driver-dependent)

Same prerequisites as X11 plus the Vulkan SDK / loader:

```
sudo apt install libvulkan-dev vulkan-tools
```

```
mkdir build_x11vk
cd build_x11vk
cmake .. -DUSE_X11=ON -DLINUX_VK=ON
cmake --build .
```

Outputs:
- `bin/linear_Linux_X11_GLX_VK`, `bin/bt2020_pq_Linux_X11_GLX_VK`, etc.

HDR availability depends on whether the running compositor and driver expose HDR colorspaces
on a Vulkan X11 surface. Most X11 compositors do not, so this variant is mainly useful for
direct rendering or future-proofing.

### Linux — Wayland + Vulkan HDR (implemented, recommended for HDR on Linux)

Requirements: same as above, plus Wayland development files:

```
sudo apt install libwayland-dev wayland-protocols pkg-config libvulkan-dev
```

```
mkdir build_wl
cd build_wl
cmake .. -DWL_EGL_PLATFORM=ON
cmake --build .
```

Outputs:
- `bin/linear_Linux_Wayland_VK`, `bin/bt2020_pq_Linux_Wayland_VK`, etc.

For working HDR on Linux, this variant requires:
- A native Linux session (not WSL2 — WSLg cannot pass HDR signals through)
- A Wayland HDR-capable compositor (KDE Plasma 6.1+ or GNOME 47+)
- An HDR-capable GPU driver (NVIDIA 555+, Mesa RADV recent, etc.)
- HDR enabled in the compositor's display settings

## Examples

| Executable (base name) | Colorspace | HDR-class |
|---|---|---|
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

The full filename includes the build variant suffix, e.g. `bt2020_pq_Windows_WGL_VK.exe`,
`bt2020_pq_Linux_Wayland_VK`. The window title likewise shows the variant in
`[OS/Windowing/Backend]` form (e.g. `EGL BT.2020 PQ [Linux/Wayland/VK]`).

Each example checks at runtime whether its colorspace is supported on the current
platform/driver and exits with a message if not, so cross-platform testing never crashes.


## Architecture

```
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
  └── egl_linux_gles.cpp   Linux — system libEGL/libGLESv2 ES backend (dlopen)
  └── egl_wayland.cpp      Linux / Unix — Wayland + xdg-shell (paired with egl_linux_vk.cpp)
  └── egl_linux_vk.cpp     Linux — Vulkan HDR presentation (shared by X11+VK and Wayland)
  └── egl_<platform>.cpp   Future backends
```


## Yours Norbert Nopper


## Changelog

14.05.2026 - Added Linux OpenGL ES backend via system libEGL/libGLESv2 (Mesa/vendor). Enabled by default with `-DEGL_LINUX_ENABLE_GLES=ON`.

14.05.2026 - Removed all non-Windows/Linux platforms (Emscripten, QNX, Fuchsia, OHOS, Apple, Android, DRM/GBM). Fixed unused-code warnings found by Clang and GCC.

03.05.2026 - Fixed OpenGL and Vulkan coordinate. v1.0.4.

03.05.2026 - Added OpenGL ES support via ANGLE on Windows. Documentation fixes. v1.0.3.

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
