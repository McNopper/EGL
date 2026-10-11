#ifndef EGL_VG_H_
#define EGL_VG_H_

/*
 * OpenVG client API provider seam.
 *
 * EGL implements the platform side of the OpenVG client API: configuration
 * selection, window surfaces, the pixel buffer and its presentation. The
 * OpenVG implementation itself lives behind this small provider interface and
 * is registered before EGL is used - McNopper/OpenVG_Library does so
 * automatically when it is linked. EGL contains no OpenVG implementation
 * code of its own.
 */

#include <EGL/egl.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * Pixel layout of the framebuffer passed to the provider: VG_sARGB_8888
 * (sRGB, non-premultiplied, 32 bits per pixel, byte order B,G,R,A), stride in
 * bytes. This is the natural window layout of the GDI and X11 backends, so
 * presenting the buffer is a straight copy.
 */
#define EGL_VG_BUFFER_FORMAT_SARGB_8888 0x0001

typedef struct EGLVGProvider
{
    /* Creates a rendering context. Returns EGL_TRUE on success. */
    EGLBoolean (EGLAPIENTRY *createContext)(void** outContext);

    /* Destroys a rendering context. */
    EGLBoolean (EGLAPIENTRY *destroyContext)(void* context);

    /*
     * Binds the framebuffer as the current drawable of the context; a NULL
     * context unbinds. The buffer stays valid until the next eglSwapBuffers
     * or until the surface is destroyed.
     */
    EGLBoolean (EGLAPIENTRY *makeCurrent)(void* context, void* framebuffer, EGLint format, EGLint width, EGLint height, EGLint strideBytes);

    /* Called after rendering and before EGL presents the buffer. Optional (may be NULL). */
    void (EGLAPIENTRY *notifySwap)(void* context);
} EGLVGProvider;

/*
 * Registers the OpenVG provider. Only one provider can be active; passing NULL
 * removes it. Register the provider before creating OpenVG contexts - at
 * startup, before the first EGL call, is the intended place.
 */
EGLAPI EGLBoolean EGLAPIENTRY eglSetVGProvider(const EGLVGProvider* provider);

#ifdef __cplusplus
}
#endif

#endif /* EGL_VG_H_ */
