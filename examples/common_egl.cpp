/**
 * Shared EGL application helpers — implementation.
 *
 * The MIT License (MIT)
 * Copyright (c) since 2014 Norbert Nopper
 */

#include "common_egl.h"

#include <GL/gl.h>
#include <stdio.h>

EGLApp* egl_app_create(const EGLint* config_attribs,
                       const EGLint* surface_attribs,
                       const char*   ext_required,
                       const char*   title,
                       int width, int height)
{
    EGLApp* app = new EGLApp{};

    app->native_dpy = __openDisplay();
    app->dpy        = eglGetDisplay(app->native_dpy);
    if (app->dpy == EGL_NO_DISPLAY)
    {
        fprintf(stderr, "eglGetDisplay failed\n");
        __closeDisplay(app->native_dpy);
        delete app;
        return nullptr;
    }

    EGLint major = 0, minor = 0;
    if (!eglInitialize(app->dpy, &major, &minor))
    {
        fprintf(stderr, "eglInitialize failed\n");
        __closeDisplay(app->native_dpy);
        delete app;
        return nullptr;
    }
    printf("EGL %d.%d\n", major, minor);

    if (ext_required)
    {
        const char* exts = eglQueryString(app->dpy, EGL_EXTENSIONS);
        if (!ext_supported(exts, ext_required))
        {
            fprintf(stderr, "%s not supported\n", ext_required);
            eglTerminate(app->dpy);
            delete app;
            return nullptr;
        }
        printf("%s: supported\n", ext_required);
    }

    eglBindAPI(EGL_OPENGL_API);

    EGLConfig cfg = nullptr;
    EGLint    ncfg = 0;
    eglChooseConfig(app->dpy, config_attribs, &cfg, 1, &ncfg);
    if (!ncfg)
    {
        fprintf(stderr, "No matching EGL config\n");
        eglTerminate(app->dpy);
        delete app;
        return nullptr;
    }

    EGLint visual_id = 0;
    eglGetConfigAttrib(app->dpy, cfg, EGL_NATIVE_VISUAL_ID, &visual_id);

    char full_title[256];
    snprintf(full_title, sizeof(full_title), "%s [%s/%s/%s]", title, __osName(), __windowingName(), __backendName());

    app->win = __createWindow(app->native_dpy, visual_id, width, height, full_title);
    if (!app->win)
    {
        fprintf(stderr, "createWindow failed\n");
        eglTerminate(app->dpy);
        delete app;
        return nullptr;
    }

    app->surf = eglCreateWindowSurface(app->dpy, cfg, __nativeWindowHandle(app->win), surface_attribs);
    if (app->surf == EGL_NO_SURFACE)
    {
        fprintf(stderr, "eglCreateWindowSurface failed: 0x%x\n", eglGetError());
        __destroyWindow(app->win);
        eglTerminate(app->dpy);
        delete app;
        return nullptr;
    }

    static const EGLint ctx_attribs[] = { EGL_NONE };
    app->ctx = eglCreateContext(app->dpy, cfg, EGL_NO_CONTEXT, ctx_attribs);
    if (app->ctx == EGL_NO_CONTEXT)
    {
        fprintf(stderr, "eglCreateContext failed\n");
        eglDestroySurface(app->dpy, app->surf);
        __destroyWindow(app->win);
        eglTerminate(app->dpy);
        delete app;
        return nullptr;
    }

    if (!eglMakeCurrent(app->dpy, app->surf, app->surf, app->ctx))
    {
        fprintf(stderr, "eglMakeCurrent failed\n");
        eglDestroyContext(app->dpy, app->ctx);
        eglDestroySurface(app->dpy, app->surf);
        __destroyWindow(app->win);
        eglTerminate(app->dpy);
        delete app;
        return nullptr;
    }

    printf("GL renderer: %s\n", (const char*)glGetString(GL_RENDERER));
    return app;
}

void egl_app_run(EGLApp* app,
                 void (*frame_cb)(EGLApp* app, void* user),
                 void* user)
{
    while (__processEvents(app->win))
        frame_cb(app, user);
}

void egl_app_destroy(EGLApp* app)
{
    if (!app)
        return;
    eglMakeCurrent(app->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(app->dpy, app->ctx);
    eglDestroySurface(app->dpy, app->surf);
    eglTerminate(app->dpy);
    __destroyWindow(app->win);
    __closeDisplay(app->native_dpy);
    delete app;
}
