EGL 1.5 desktop implementation (Windows):
------------------------------------------

Since EGL version 1.5 (https://www.khronos.org/registry/egl/), it is possible to create an OpenGL context with EGL.
This library implements EGL 1.5 for Windows by wrapping WGL. Only OpenGL is supported.
The purpose of this library is to have a single EGL-based surface creation and context management path
when developing OpenGL applications on Windows.

The full EGL 1.5 API surface is implemented, including window surfaces, pbuffer surfaces, pixmap surfaces,
rendering contexts, sync objects, and image objects. The standard initialization sequence described at
https://www.khronos.org/registry/egl/sdk/docs/man/html/eglIntro.xhtml works on Windows.

HDR support:
The library supports HDR output via a Vulkan presentation backend (requires Vulkan SDK). The following
EGL colorspace extensions are supported where the GPU driver and display allow it:

   EGL_EXT_gl_colorspace_scrgb_linear   scRGB linear (fp16, R16G16B16A16_SFLOAT)
   EGL_EXT_gl_colorspace_scrgb          scRGB gamma (fp16, R16G16B16A16_SFLOAT)
   EGL_EXT_gl_colorspace_bt2020_pq      BT.2020 PQ / HDR10 (10-bit, A2B10G10R10_UNORM)
   EGL_EXT_gl_colorspace_bt2020_linear  BT.2020 linear (fp16, R16G16B16A16_SFLOAT)
   EGL_EXT_gl_colorspace_bt2020_hlg     BT.2020 HLG (10-bit, A2B10G10R10_UNORM)

Supported colorspaces are probed at eglInitialize time and advertised via eglQueryString(EGL_EXTENSIONS).
If a colorspace is not supported, eglCreateWindowSurface returns EGL_BAD_MATCH — there is no fallback.

How to build EGL:

1. Install CMake (3.10 or newer), MSVC (Visual Studio 2019+), and the Vulkan SDK.
2. Create a build directory and run CMake:

   mkdir build
   cd build
   cmake ..
   cmake --build .

Example (out-of-source, Release build):

   cmake -DCMAKE_BUILD_TYPE=Release ..
   cmake --build . --config Release

The build also compiles the bundled examples in bin/:

   linear          SDR linear colorspace
   srgb            SDR sRGB colorspace
   scrgb_linear    HDR scRGB linear
   scrgb           HDR scRGB gamma
   bt2020_pq       HDR BT.2020 PQ (HDR10)
   bt2020_linear   HDR BT.2020 linear
   bt2020_hlg      HDR BT.2020 HLG

Each example checks whether its colorspace is supported and exits with a console message if not.
No additional dependencies are required beyond the EGL library and Vulkan SDK.

If you get build errors:

- Please make sure the Vulkan SDK is installed and VULKAN_SDK is set in your environment.
- MSVC is required; GCC/MinGW/Clang are not supported on Windows for this build.


Yours Norbert Nopper


Changelog:

18.04.2026 - Added HDR support via Vulkan presentation backend. Five EGL colorspace extensions supported (scrgb_linear, scrgb, bt2020_pq, bt2020_linear, bt2020_hlg). Colorspace availability probed at eglInitialize time using test swapchains; only advertised if the driver can actually create the swapchain. No fallback — eglCreateWindowSurface returns EGL_BAD_MATCH for unsupported colorspaces. Dropped Linux/X11 support; library is now Windows-only.

14.04.2026 - Added EGL_KHR_gl_colorspace extension support: eglQueryString(EGL_EXTENSIONS) now returns "EGL_KHR_gl_colorspace", EGL_GL_COLORSPACE_KHR is stored per surface and returned by eglQuerySurface, and pixmap surface creation now validates and stores the colorspace attribute on both Windows and X11.

12.04.2026 - Fixed 19 EGL 1.5 spec compliance issues: correct return values from eglDestroyContext/eglDestroySurface/eglTerminate, two-call pattern for eglChooseConfig/eglGetConfigs (configs=NULL returns count), lexicographic version comparison in eglCreateContext, attrib_list=NULL treated as empty, EGL_TRANSPARENT_TYPE value validation, ES conformance bit selected per requested version, eglQueryContext CLIENT_TYPE/CLIENT_VERSION, eglSwapBuffers returns EGL_FALSE, WGL software renderer maps to EGL_SLOW_CONFIG, and EGL_LEVEL allows negative values.

12.04.2026 - Added green window example(examples/green_window). Fixed EGL_STENCIL_SIZE config matching to use minimum-value semantics per spec (was exact match, causing eglChooseConfig to return no configs on D24S8 hardware). Removed GLEW dependency entirely; the library now has zero external dependencies.

12.04.2026 - Implemented full EGL 1.5 API: window/pbuffer/pixmap surfaces, sync objects (GL_ARB_sync), image objects, eglCopyBuffers, eglBindTexImage/eglReleaseTexImage, eglGetPlatformDisplay, platform window/pixmap surfaces, and all remaining surface/config queries.

12.04.2026 - Switched build system to CMake (3.10+). Updated Khronos headers to latest (egl.h 2026-03-19, eglext.h 20260319). Library output renamed to libEGL per Khronos convention.

29.01.2015 - Updated to GLEW 1.12.0. Current version: v0.3.3.

25.01.2015 - Fixed bug during initialization under Windows. Current version: v0.3.3.

20.01.2015 - Added GLX version check. Fixed bug in window creation under X11. Current version: v0.3.2.

05.12.2014 - Removed duplicate code. Current version: v0.3.1.

04.12.2014 - Working X11 version. Current version: v0.3.0.

28.11.2014 - Continued on X11 version. Current version: v0.2.3.

22.11.2014 - X11 compiling but not complete. Current version: v0.2.2.

18.11.2014 - Added X11 build configuration and started implementing it. X11 not compiling yet. Current version: v0.2.1.

17.11.2014 - Released first public version: v0.2.
