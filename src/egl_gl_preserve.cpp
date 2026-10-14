/**
 * Preserved swap-behavior emulation for backends whose native swap destroys
 * or exchanges the back buffer (glXSwapBuffers, WGL SwapBuffers).
 *
 * EGL 1.5 section 3.5.5: with EGL_BUFFER_PRESERVED the color-buffer contents
 * are unaffected by posting. The helper captures the back buffer around the
 * native swap - read before, redraw after - and keeps the client's rendering
 * state intact across the copy. All GL entry points are resolved through the
 * backend's resolver, so the library itself carries no GL linkage.
 *
 * The MIT License (MIT)
 *
 * Copyright (c) since 2014 Norbert Nopper
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "egl_gl_preserve.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <GL/gl.h>
#include <stdlib.h>

typedef void (*PFN_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
typedef void (*PFN_glDrawPixels)(GLsizei, GLsizei, GLenum, GLenum, const void*);
typedef void (*PFN_glPushAttrib)(GLbitfield);
typedef void (*PFN_glPopAttrib)(void);
typedef void (*PFN_glPushClientAttrib)(GLbitfield);
typedef void (*PFN_glPopClientAttrib)(void);
typedef void (*PFN_glWindowPos2i)(GLint, GLint);
typedef void (*PFN_glDisable)(GLenum);
typedef void (*PFN_glPixelStorei)(GLenum, GLint);

static EGLGLPreserveResolver s_resolver = nullptr;
static int                    s_resolved  = 0;
static PFN_glReadPixels       s_glReadPixels;
static PFN_glDrawPixels       s_glDrawPixels;
static PFN_glPushAttrib       s_glPushAttrib;
static PFN_glPopAttrib        s_glPopAttrib;
static PFN_glPushClientAttrib s_glPushClientAttrib;
static PFN_glPopClientAttrib  s_glPopClientAttrib;
static PFN_glWindowPos2i      s_glWindowPos2i;
static PFN_glDisable          s_glDisable;
static PFN_glPixelStorei      s_glPixelStorei;

static void resolveOnce(void)
{
    if (s_resolved || !s_resolver)
        return;
    s_resolved = 1;

    s_glReadPixels       = (PFN_glReadPixels)s_resolver("glReadPixels");
    s_glDrawPixels       = (PFN_glDrawPixels)s_resolver("glDrawPixels");
    s_glPushAttrib       = (PFN_glPushAttrib)s_resolver("glPushAttrib");
    s_glPopAttrib        = (PFN_glPopAttrib)s_resolver("glPopAttrib");
    s_glPushClientAttrib = (PFN_glPushClientAttrib)s_resolver("glPushClientAttrib");
    s_glPopClientAttrib  = (PFN_glPopClientAttrib)s_resolver("glPopClientAttrib");
    // ARB_window_pos draws at exact window coordinates regardless of the
    // client's matrices; without it a restored raster position cannot be
    // guaranteed, so preservation is not attempted at all.
    s_glWindowPos2i      = (PFN_glWindowPos2i)s_resolver("glWindowPos2i");
    s_glDisable          = (PFN_glDisable)s_resolver("glDisable");
    s_glPixelStorei      = (PFN_glPixelStorei)s_resolver("glPixelStorei");
}

void glPreserveSetResolver(EGLGLPreserveResolver resolver)
{
    s_resolver = resolver;
    s_resolved = 0;
}

void* glPreserveBegin(EGLint width, EGLint height)
{
    resolveOnce();

    if (!s_glReadPixels || !s_glDrawPixels || !s_glPushAttrib || !s_glPopAttrib ||
        !s_glPushClientAttrib || !s_glPopClientAttrib || !s_glWindowPos2i ||
        !s_glDisable || !s_glPixelStorei)
    {
        return nullptr;
    }

    if (width <= 0 || height <= 0)
        return nullptr;

    // RGBA/FLOAT round-trips every color-buffer precision exactly (8, 10, 16
    // and 32 bit, integer and fixed-point), unlike a byte readback.
    void* buffer = malloc((size_t)width * (size_t)height * 4 * sizeof(float));
    if (!buffer)
        return nullptr;

    // Everything the copy must not disturb is saved and disabled; the swap
    // itself happens between the paired Begin/End calls on the same context.
    s_glPushAttrib(GL_ALL_ATTRIB_BITS);
    s_glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);

    s_glDisable(GL_SCISSOR_TEST);
    s_glDisable(GL_BLEND);
    s_glDisable(GL_DITHER);
    s_glDisable(GL_ALPHA_TEST);
    s_glDisable(GL_DEPTH_TEST);
    s_glDisable(GL_STENCIL_TEST);
    s_glDisable(GL_CULL_FACE);
    s_glDisable(GL_COLOR_LOGIC_OP);
    for (int i = 0; i < 6; i++)
        s_glDisable(GL_CLIP_PLANE0 + i);
#ifdef GL_COLOR_MATRIX
    s_glDisable(GL_COLOR_MATRIX);
#endif
#ifdef GL_RASTERIZER_DISCARD
    s_glDisable(GL_RASTERIZER_DISCARD);
#endif
#ifdef GL_MULTISAMPLE
    s_glDisable(GL_MULTISAMPLE);
#endif
    s_glPixelStorei(GL_PACK_ALIGNMENT, 4);
    s_glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    s_glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, buffer);

    return buffer;
}

void glPreserveEnd(EGLint width, EGLint height, void* buffer)
{
    if (!buffer)
        return;

    s_glWindowPos2i(0, 0);
    s_glDrawPixels(width, height, GL_RGBA, GL_FLOAT, buffer);

    s_glPopClientAttrib();
    s_glPopAttrib();

    free(buffer);
}
