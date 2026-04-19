# EGL 1.5 Implementation

A portable, spec-compliant implementation of [EGL 1.5](https://www.khronos.org/registry/egl/) built around
a platform-agnostic core and pluggable OS/rendering backends. The library provides the standard EGL
surface creation, context management, and synchronization API across operating systems, so that
applications can share a single EGL-based rendering path regardless of the underlying platform.

The Khronos headers bundled with the library are the official, unmodified ones from the
[EGL Registry](https://github.com/KhronosGroup/EGL-Registry) (commit e80a2e0050, 2026-03-19).


## Platform and Backend Support

| Operating System | Backend | Status | Notes |
|---|---|---|---|
| Windows | WGL (OpenGL) | **Implemented** | Default sRGB and linear colorspaces |
| Windows | Vulkan (HDR) | **Implemented** | HDR colorspace extensions via Vulkan swapchain |
| macOS / iOS | CGL / EAGL / Metal | Prepared | Stub ready in `egl_internal.h` |
| Android | ANativeWindow | Prepared | Stub ready in `egl_internal.h` |
| Linux / Unix — X11 | GLX | Prepared | Stub ready; default when no sub-platform flag set |
| Linux — Wayland | EGL native | Prepared | Build with `-DWL_EGL_PLATFORM=1` |
| Linux — DRM/KMS | GBM | Prepared | Build with `-DGBM_PLATFORM=1` |
| Linux — ChromeOS | Ozone | Prepared | Build with `-DOZONE_PLATFORM=1` |
| QNX | Screen API | Prepared | Stub ready in `egl_internal.h` |
| HarmonyOS (OHOS) | Native | Prepared | Stub ready in `egl_internal.h` |
| Haiku | Native | Prepared | Stub ready in `egl_internal.h` |
| Fuchsia | Native | Prepared | Stub ready in `egl_internal.h` |
| WebAssembly | Emscripten / WebGL | Prepared | Stub ready in `egl_internal.h` |
| Symbian (legacy) | Native | Not planned | Stub ready in `egl_internal.h` |

Adding a new backend requires implementing 17 backend functions declared in `src/egl_internal.h`
(prefixed `__`) and adding a source file entry to `CMakeLists.txt`.


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


## HDR Support (Windows)

The Windows backend supports HDR output via a Vulkan presentation layer. The following colorspace
extensions are probed at `eglInitialize` time and advertised only if the driver and display support them:

| Extension | Colorspace | Format |
|---|---|---|
| `EGL_EXT_gl_colorspace_scrgb_linear` | scRGB linear | R16G16B16A16_SFLOAT |
| `EGL_EXT_gl_colorspace_scrgb` | scRGB gamma | R16G16B16A16_SFLOAT |
| `EGL_EXT_gl_colorspace_bt2020_pq` | BT.2020 PQ / HDR10 | A2B10G10R10_UNORM |
| `EGL_EXT_gl_colorspace_bt2020_linear` | BT.2020 linear | R16G16B16A16_SFLOAT |
| `EGL_EXT_gl_colorspace_bt2020_hlg` | BT.2020 HLG | A2B10G10R10_UNORM |

If a requested colorspace is not supported, `eglCreateWindowSurface` returns `EGL_BAD_MATCH`.
There is no silent fallback.


## Building

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

### Other platforms (planned)

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

Non-Windows builds will compile until the link stage and then fail on the 17 unimplemented `__`
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

  Platform backends (implement the 17 __ functions declared in egl_internal.h):
  └── egl_windows.cpp      Windows — WGL
  └── egl_windows_vk.cpp   Windows — Vulkan HDR presentation
  └── egl_<platform>.cpp   Future backends
```


## Yours Norbert Nopper


## Changelog

19.04.2026 - Rewrote README to reflect the general multi-platform EGL architecture. Added platform/backend
             support table. All Khronos headers verified as latest (e80a2e0050, 2026-03-19). Added platform
             stubs for QNX, Emscripten, Wayland, GBM, Ozone, Android, Haiku, Fuchsia, HarmonyOS, Symbian
             to egl_internal.h. CMakeLists.txt updated with platform-conditional branches for all supported OSes.

18.04.2026 - Added HDR support via Vulkan presentation backend. Five EGL colorspace extensions supported
             (scrgb_linear, scrgb, bt2020_pq, bt2020_linear, bt2020_hlg). Colorspace availability probed
             at eglInitialize time using test swapchains. No fallback — eglCreateWindowSurface returns
             EGL_BAD_MATCH for unsupported colorspaces.

18.04.2026 - Refactored monolithic egl_common.cpp (~3900 lines) into eight focused modules:
             egl_globals, egl_config, egl_display, egl_context, egl_surface, egl_sync, egl_image, egl_api.
             Extracted Vulkan HDR backend into egl_windows_vk.cpp.

14.04.2026 - Added EGL_KHR_gl_colorspace extension support.

12.04.2026 - Fixed 19 EGL 1.5 spec compliance issues. Added green_window example. Removed GLEW dependency.
             Implemented full EGL 1.5 API. Switched build system to CMake 3.10+. Updated Khronos headers
             to latest (egl.h 2026-03-19, eglext.h 20260319).

29.01.2015 - Updated to GLEW 1.12.0. v0.3.3.

25.01.2015 - Fixed initialization bug on Windows. v0.3.3.

20.01.2015 - Added GLX version check. Fixed window creation bug on X11. v0.3.2.

05.12.2014 - Removed duplicate code. v0.3.1.

04.12.2014 - Working X11 version. v0.3.0.

28.11.2014 - Continued X11 implementation. v0.2.3.

22.11.2014 - X11 compiling but not complete. v0.2.2.

18.11.2014 - Added X11 build configuration. v0.2.1.

17.11.2014 - First public release. v0.2.
