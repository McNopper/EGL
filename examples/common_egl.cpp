/**
 * Shared EGL application helpers — implementation.
 *
 * The MIT License (MIT)
 * Copyright (c) since 2014 Norbert Nopper
 */

#include "common_egl.h"

#include <GL/gl.h>
#include <stdio.h>

namespace
{

// Unwinds a partially built EGLApp on the early exits. The manual ladder grew
// a longer teardown tail per creation step and left the native display open on
// all but its first exit.
struct AppBuilder
{
    EGLApp* app;

    bool keep = false;

    ~AppBuilder()
    {
        if (keep || !app)
        {
            return;
        }

        if (app->ctx != EGL_NO_CONTEXT)
        {
            eglDestroyContext(app->dpy, app->ctx);
        }
        if (app->surf != EGL_NO_SURFACE)
        {
            eglDestroySurface(app->dpy, app->surf);
        }
        if (app->win)
        {
            __destroyWindow(app->win);
        }
        if (app->dpy != EGL_NO_DISPLAY)
        {
            eglTerminate(app->dpy);
        }
        if (app->native_dpy)
        {
            __closeDisplay(app->native_dpy);
        }

        delete app;
    }
};

} // namespace

EGLApp* egl_app_create(const EGLint* config_attribs,
                       const EGLint* surface_attribs,
                       const char*   ext_required,
                       const char*   title,
                       int width, int height,
                       EGLenum       api)
{
    AppBuilder builder;

    builder.app = new EGLApp{};

    EGLApp* app = builder.app;

    app->native_dpy = __openDisplay();
    app->dpy        = eglGetDisplay(app->native_dpy);
    if (app->dpy == EGL_NO_DISPLAY)
    {
        fprintf(stderr, "eglGetDisplay failed\n");
        return nullptr;
    }

    EGLint major = 0, minor = 0;
    if (!eglInitialize(app->dpy, &major, &minor))
    {
        fprintf(stderr, "eglInitialize failed\n");
        return nullptr;
    }
    printf("EGL %d.%d\n", major, minor);

    if (ext_required)
    {
        const char* exts = eglQueryString(app->dpy, EGL_EXTENSIONS);
        if (!ext_supported(exts, ext_required))
        {
            fprintf(stderr, "%s not supported\n", ext_required);
            return nullptr;
        }
        printf("%s: supported\n", ext_required);
    }

    eglBindAPI(api);
    const char* apiTag = (api == EGL_OPENGL_ES_API) ? "ES" : "GL";

    EGLConfig cfg = nullptr;
    EGLint    ncfg = 0;
    eglChooseConfig(app->dpy, config_attribs, &cfg, 1, &ncfg);
    if (!ncfg)
    {
        fprintf(stderr, "No matching EGL config\n");
        return nullptr;
    }

    EGLint visual_id = 0;
    eglGetConfigAttrib(app->dpy, cfg, EGL_NATIVE_VISUAL_ID, &visual_id);

    char full_title[256];
    if (api == EGL_OPENGL_ES_API)
    {
#if defined(_WIN32)
        snprintf(full_title, sizeof(full_title), "%s [%s/%s/ANGLE]", title,
                 apiTag, __osName());
#else
        snprintf(full_title, sizeof(full_title), "%s [%s/%s/GLES]", title,
                 apiTag, __osName());
#endif
    }
    else
    {
        snprintf(full_title, sizeof(full_title), "%s [%s/%s/%s/%s]", title,
                 apiTag, __osName(), __windowingName(), __backendName());
    }

    app->win = __createWindow(app->native_dpy, visual_id, width, height, full_title);
    if (!app->win)
    {
        fprintf(stderr, "createWindow failed\n");
        return nullptr;
    }

    app->surf = eglCreateWindowSurface(app->dpy, cfg, __nativeWindowHandle(app->win), surface_attribs);
    if (app->surf == EGL_NO_SURFACE)
    {
        fprintf(stderr, "eglCreateWindowSurface failed: 0x%x\n", eglGetError());
        return nullptr;
    }

    static const EGLint default_ctx_attribs_gl[]  = { EGL_NONE };
    static const EGLint default_ctx_attribs_es3[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 0,
        EGL_NONE
    };
    const EGLint* ctx_attribs = (api == EGL_OPENGL_ES_API) ? default_ctx_attribs_es3
                                                           : default_ctx_attribs_gl;
    app->ctx = eglCreateContext(app->dpy, cfg, EGL_NO_CONTEXT, ctx_attribs);
    if (app->ctx == EGL_NO_CONTEXT)
    {
        fprintf(stderr, "eglCreateContext failed\n");
        return nullptr;
    }

    if (!eglMakeCurrent(app->dpy, app->surf, app->surf, app->ctx))
    {
        fprintf(stderr, "eglMakeCurrent failed\n");
        return nullptr;
    }

    printf("GL renderer: %s\n", (const char*)glGetString(GL_RENDERER));
    (void)apiTag;

    builder.keep = true;

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
