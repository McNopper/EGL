/**
 * Preserved swap-behavior emulation for backends whose native swap destroys
 * or exchanges the back buffer (glXSwapBuffers, WGL SwapBuffers).
 *
 * EGL 1.5 section 3.5.5: with EGL_BUFFER_PRESERVED the color-buffer contents
 * are unaffected by posting. The helper captures the back buffer around the
 * native swap - read before, redraw after - and keeps the client's rendering
 * state intact across the copy.
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

#ifndef EGL_GL_PRESERVE_H
#define EGL_GL_PRESERVE_H

#include <EGL/egl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Resolves a GL entry point by name (glXGetProcAddressARB, wglGetProcAddress
 * plus GetProcAddress for GL 1.1 symbols, ...). Registered once by the
 * backend before the first preserved swap.
 */
typedef void* (*EGLGLPreserveResolver)(const char* name);

void glPreserveSetResolver(EGLGLPreserveResolver resolver);

/**
 * Reads the current back buffer into an opaque copy with the rendering state
 * that would corrupt it (scissor, blend, dither, ...) disabled and saved.
 * Returns NULL when preservation is not possible (no resolver, a missing
 * entry point, a degenerate size or allocation failure); nothing is pushed
 * in that case and the caller simply swaps without preservation.
 */
void* glPreserveBegin(EGLint width, EGLint height);

/**
 * Redraws the saved contents into the new back buffer, restores the saved
 * state and releases the buffer. buffer may be NULL, in which case this is
 * a no-op (also used when the swap itself failed).
 */
void glPreserveEnd(EGLint width, EGLint height, void* buffer);

#ifdef __cplusplus
}
#endif

#endif /* EGL_GL_PRESERVE_H */
