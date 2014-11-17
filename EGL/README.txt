EGL 1.5 desktop implementation:
-------------------------------

Since EGL version 1.5 (https://www.khronos.org/registry/egl/), it is possible to create an OpenGL context with EGL.
This library implements EGL 1.5 for Windows and X11 by wrapping WGL and GLX. Only OpenGL is supported.
The purpose of this library and wrapping the existing APIs is to have the same source code on embedded and desktop systems
when developing OpenGL applications.

The project is in early stage and not well tested, but an initialization as seen on
https://www.khronos.org/registry/egl/sdk/docs/man/html/eglIntro.xhtml under Windows does already work.

How to build EGL:

1. Install Eclipse IDE for C/C++ Developers and a GNU Compiler Collection for your operating system.
2. Import EGL as an existing project.
3. Set the build configuration in Eclipse to your operating system.
4. Build EGL.

If you get build errors:

- Please make sure, that you install all the needed header and libraries.

SDKs and Libraries:

- GLEW 1.11.0 http://glew.sourceforge.net/

Build configuration naming:

[CPU]_[GPU]_[Window System]_[OpenGL]_[Compiler]_[Configuration]

CPU:								x64, x86
GPU/Emulator (Optional):
Window System: 						Windows, X11
OpenGL (Optional):
Compiler:							GCC, MinGW
Configuration:						Debug, Release

e.g. x86__Windows__MinGW_Debug


Yours Norbert Nopper


TODOs:

- Check, if needed GL/WGL version is available. Otherwise this EGL lib will crash.

- Implement TODOs as marked in the source code.

- Port to desktop Linux X11.

- Remove dublicate code.

- Implement FIXMEs as marked in the source code.


Changelog:

17.11.2014 - Released first public version: v0.2.

