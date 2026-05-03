# EGL 1.5 Implementation

A portable, spec-compliant implementation of [EGL 1.5](https://www.khronos.org/registry/egl/) built around
a platform-agnostic core and pluggable OS/rendering backends. The library provides the standard EGL
surface creation, context management, and synchronization API across operating systems, so that
applications can share a single EGL-based rendering path regardless of the underlying platform.

The Khronos headers bundled with the library are the official, unmodified ones from the
[EGL Registry](https://github.com/KhronosGroup/EGL-Registry) (commit e80a2e0050, 2026-03-19).


## Platform and Backend Support

| Operating System | Supported APIs | Platform API | Backend API | Status Quo |
|---|---|---|---|---|
| Windows | OpenGL | WGL, DXGI | Vulkan | **Implemented** |
| Linux — X11 | OpenGL | GLX | - | **Implemented** |
| macOS / iOS | - | CGL, EAGL, CAMetalLayer | - | Prepared |
| Android | - | ANativeWindow | - | Prepared |
| Linux — Wayland | - | wl_egl_window | - | Prepared |
| Linux — DRM/KMS | - | GBM, libdrm | - | Prepared |
| Linux — ChromeOS | - | Ozone | - | Prepared |
| QNX | - | Screen API | - | Prepared |
| HarmonyOS (OHOS) | - | OHNativeWindow | - | Prepared |
| Haiku | - | BGLView | - | Prepared |
| Fuchsia | - | Scenic / Flatland | - | Prepared |
| WebAssembly | - | Emscripten | - | Prepared |
| Symbian (legacy) | - | Native | - | Prepared |

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

### Linux — X11/GLX (implemented)

Requirements: CMake 3.10+, GCC/Clang, X11 and OpenGL development headers.

On Ubuntu/Debian:

```
sudo apt install build-essential cmake ninja-build \
    libx11-dev libxext-dev libgl-dev libglu1-mesa-dev
```

```
mkdir build-linux
cd build-linux
cmake .. -G Ninja
ninja
```

Outputs: `lib/libEGL.a` and example executables `bin/linear` and `bin/srgb`.

To run under WSL2 on Windows 11, WSLg provides the display server automatically — no additional
setup is required. Launch examples directly from the WSL2 shell:

```
./bin/linear
./bin/srgb
```

> **Note:** HDR examples (`scrgb_*`, `bt2020_*`, `display_p3_*`) are Windows-only and are excluded
> from the Linux build.

### Windows (implemented)

Requirements: CMake 3.10+, MSVC (Visual Studio 2019+), Vulkan SDK.

```
mkdir build
cd build
cmake ..
cmake --build .
```

Release build:

```
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
```

Outputs: `lib/libEGL.lib` and the example executables under `bin/`.

If CMake cannot find the Vulkan SDK, ensure `VULKAN_SDK` is set in your environment.

### Other platforms (prepared)

Cross-compile with the target toolchain. CMake selects the correct platform branch automatically
based on `CMAKE_SYSTEM_NAME`. For Linux sub-platforms pass the appropriate define:

```
# Wayland
cmake .. -DWL_EGL_PLATFORM=1

# GBM / DRM-KMS
cmake .. -DGBM_PLATFORM=1

# Ozone
cmake .. -DOZONE_PLATFORM=1
```

Non-X11 Unix builds will compile until the link stage and then fail on the unimplemented `__`
backend functions — this is intentional and indicates where the new backend code goes.


## Examples

| Executable | Colorspace | HDR |
|---|---|---|
| `srgb` | sRGB | No |
| `linear` | Linear | No |
| `scrgb_linear` | scRGB linear | Yes |
| `scrgb` | scRGB gamma | Yes |
| `bt2020_pq` | BT.2020 PQ / HDR10 | Yes |
| `bt2020_linear` | BT.2020 linear | Yes |
| `bt2020_hlg` | BT.2020 HLG | Yes |
| `display_p3` | Display P3 (sRGB EOTF) | No |
| `display_p3_linear` | Display P3 linear | No |
| `display_p3_passthrough` | Display P3 passthrough | No |

Each example checks at runtime whether its colorspace is supported and exits with a message if not.


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
  └── wglext.h             WGL extension prototypes (Windows)

  Platform backends (implement the functions declared in egl_internal.h):
  └── egl_windows.cpp      Windows — WGL
  └── egl_windows_vk.cpp   Windows — Vulkan HDR presentation
  └── egl_x11_glx.cpp      Linux / Unix — X11 + GLX
  └── egl_<platform>.cpp   Future backends
```


## Yours Norbert Nopper


## Changelog

19.04.2026 - Added Linux X11/GLX backend. v1.1.0.

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
