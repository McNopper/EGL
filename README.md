EGL 1.5 desktop implementation:
-------------------------------

Since EGL version 1.5 (https://www.khronos.org/registry/egl/), it is possible to create an OpenGL context with EGL.
This library implements EGL 1.5 for Windows and X11 by wrapping WGL and GLX. Only OpenGL is supported.
The purpose of this library and wrapping the existing APIs is to have the same source code on embedded and desktop systems
when developing OpenGL applications.

The full EGL 1.5 API surface is implemented, including window surfaces, pbuffer surfaces, pixmap surfaces,
rendering contexts, sync objects, and image objects. The standard initialization sequence described at
https://www.khronos.org/registry/egl/sdk/docs/man/html/eglIntro.xhtml works on both Windows and X11.

How to build EGL:

1. Install CMake (3.10 or newer) and a C/C++ compiler (GCC, MinGW, MSVC, or Clang).
2. Create a build directory and run CMake:

   mkdir build
   cd build
   cmake ..
   cmake --build .

CMake options:

   EGL_UNIX_USE_WAYLAND  Define functions for Wayland platform (Linux only, default: OFF)

Example (out-of-source, Release build):

   cmake -DCMAKE_BUILD_TYPE=Release ..
   cmake --build . --config Release

The build also compiles the bundled examples (bin/green_window). These require no additional
dependencies beyond the EGL library itself.

If you get build errors:

- Please make sure that you have installed all needed headers and libraries.


Yours Norbert Nopper


Changelog:

12.04.2026 - Added green window example (examples/green_window). Fixed EGL_STENCIL_SIZE config matching to use minimum-value semantics per spec (was exact match, causing eglChooseConfig to return no configs on D24S8 hardware). Removed GLEW dependency entirely; the library now has zero external dependencies.

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
