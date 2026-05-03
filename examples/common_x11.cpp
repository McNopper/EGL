/**
 * Platform window helpers — X11 implementation.
 *
 * The MIT License (MIT)
 * Copyright (c) since 2014 Norbert Nopper
 */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include "common.h"

struct __NativeWindow
{
    Display* dpy;
    Window   win;
    Atom     wmDelete;
    bool     running;
};

EGLNativeDisplayType __openDisplay()
{
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy)
    {
        fprintf(stderr, "XOpenDisplay failed\n");
        exit(1);
    }
    return reinterpret_cast<EGLNativeDisplayType>(dpy);
}

__NativeWindow* __createWindow(EGLNativeDisplayType nativeDpy, EGLint visual_id,
                                int width, int height, const char* title)
{
    Display* dpy = reinterpret_cast<Display*>(nativeDpy);

    XVisualInfo tmpl  = {};
    tmpl.visualid     = static_cast<VisualID>(visual_id);
    int nvis          = 0;
    XVisualInfo* vi   = XGetVisualInfo(dpy, VisualIDMask, &tmpl, &nvis);
    if (!vi)
        return nullptr;

    Colormap cmap = XCreateColormap(dpy, RootWindow(dpy, vi->screen), vi->visual, AllocNone);

    XSetWindowAttributes swa = {};
    swa.colormap     = cmap;
    swa.border_pixel = 0;
    swa.event_mask   = KeyPressMask | StructureNotifyMask;

    Window win = XCreateWindow(dpy, RootWindow(dpy, vi->screen),
                               0, 0, static_cast<unsigned>(width), static_cast<unsigned>(height),
                               0, vi->depth, InputOutput, vi->visual,
                               CWBorderPixel | CWColormap | CWEventMask, &swa);
    XFree(vi);
    if (!win)
        return nullptr;

    Atom wmDelete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wmDelete, 1);
    XStoreName(dpy, win, title);
    XMapWindow(dpy, win);
    XSync(dpy, False);

    return new __NativeWindow{ dpy, win, wmDelete, true };
}

EGLNativeWindowType __nativeWindowHandle(__NativeWindow* win)
{
    return static_cast<EGLNativeWindowType>(win->win);
}

bool __processEvents(__NativeWindow* win)
{
    while (XPending(win->dpy))
    {
        XEvent ev;
        XNextEvent(win->dpy, &ev);

        if (ev.type == ClientMessage)
        {
            if (static_cast<Atom>(ev.xclient.data.l[0]) == win->wmDelete)
                win->running = false;
        }
        else if (ev.type == KeyPress)
        {
            if (XLookupKeysym(&ev.xkey, 0) == XK_Escape)
                win->running = false;
        }
        else if (ev.type == DestroyNotify && ev.xdestroywindow.window == win->win)
        {
            win->running = false;
        }
    }
    return win->running;
}

void __destroyWindow(__NativeWindow* win)
{
    if (!win)
        return;
    XDestroyWindow(win->dpy, win->win);
    delete win;
}

void __closeDisplay(EGLNativeDisplayType nativeDpy)
{
    if (nativeDpy)
        XCloseDisplay(reinterpret_cast<Display*>(nativeDpy));
}

const char* __osName() { return "Linux"; }
