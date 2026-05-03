#ifndef EGL_CTXINTERNALS_H_
#define EGL_CTXINTERNALS_H_

#if defined(_WIN32) || defined(__VC32__) && !defined(__CYGWIN__) && !defined(__SCITECH_SNAP__) /* Win32 and WinCE */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>

struct _EGLContextInternals
{
    HDC display;
    HDC surface;
    HGLRC context;
};

#elif defined(WL_EGL_PLATFORM) || defined(USE_X11) || defined(__unix__)

/* X11/GLX (X11 backend and Wayland backend both use GLX context internals) */
#include <X11/Xlib.h>
#include <X11/Xutil.h>

struct _EGLContextInternals
{
    Display* display;
    struct {
        GLXDrawable drawable;
        GLXFBConfig config;
    } surface;
    GLXContext context;
};

#endif

typedef struct _EGLContextInternals EGLContextInternals;

#endif