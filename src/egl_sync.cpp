#include "egl_common.h"
#include <new>

extern "C"
{

    EGLSync _eglCreateSync(EGLDisplay dpy, EGLenum type, const EGLAttrib* attrib_list)
    {
        // Per EGL 1.5 §3.8.1: for EGL_SYNC_FENCE no attributes are defined;
        // a non-empty attrib_list must generate EGL_BAD_ATTRIBUTE.
        if (attrib_list && attrib_list[0] != EGL_NONE)
        {
            g_localStorage.error = EGL_BAD_ATTRIBUTE;
            return EGL_NO_SYNC;
        }
        auto            _rl       = g_globalStorage.placeRootDpy_readlock();
        EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;
        while (walkerDpy)
        {
            if (reinterpret_cast<EGLDisplay>(walkerDpy) == dpy)
            {
                if (!walkerDpy->initialized)
                {
                    g_localStorage.error = EGL_NOT_INITIALIZED;
                    return EGL_NO_SYNC;
                }
                if (type != EGL_SYNC_FENCE)
                {
                    g_localStorage.error = EGL_BAD_ATTRIBUTE;
                    return EGL_NO_SYNC;
                }
                // EGL 1.5 §3.8.1: EGL_BAD_MATCH if no current context.
                if (g_localStorage.currentCtx == EGL_NO_CONTEXT_IMPL)
                {
                    g_localStorage.error = EGL_BAD_MATCH;
                    return EGL_NO_SYNC;
                }
                if (!glFenceSync_PTR)
                {
                    g_localStorage.error = EGL_BAD_MATCH;
                    return EGL_NO_SYNC;
                }
                void* glSync = glFenceSync_PTR(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
                if (!glSync)
                {
                    g_localStorage.error = EGL_BAD_ALLOC;
                    return EGL_NO_SYNC;
                }
                EGLSyncImpl* newSync = new (std::nothrow) EGLSyncImpl();
                if (!newSync)
                {
                    if (glDeleteSync_PTR)
                        glDeleteSync_PTR(glSync);
                    g_localStorage.error = EGL_BAD_ALLOC;
                    return EGL_NO_SYNC;
                }
                newSync->type   = EGL_SYNC_FENCE;
                newSync->glSync = glSync;
                std::lock_guard<std::mutex> lk(walkerDpy->mutex);
                newSync->next       = walkerDpy->rootSync;
                walkerDpy->rootSync = newSync;
                g_localStorage.error = EGL_SUCCESS;
                return reinterpret_cast<EGLSync>(newSync);
            }
            walkerDpy = walkerDpy->next;
        }
        g_localStorage.error = EGL_BAD_DISPLAY;
        return EGL_NO_SYNC;
    }

    EGLBoolean _eglDestroySync(EGLDisplay dpy, EGLSync sync)
    {
        auto            _rl       = g_globalStorage.placeRootDpy_readlock();
        EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;
        while (walkerDpy)
        {
            if (reinterpret_cast<EGLDisplay>(walkerDpy) == dpy)
            {
                if (!walkerDpy->initialized)
                {
                    g_localStorage.error = EGL_NOT_INITIALIZED;
                    return EGL_FALSE;
                }
                std::lock_guard<std::mutex> lk(walkerDpy->mutex);
                EGLSyncImpl*                prev   = nullptr;
                EGLSyncImpl*                walker = walkerDpy->rootSync;
                while (walker)
                {
                    if (reinterpret_cast<EGLSync>(walker) == sync)
                    {
                        if (prev)
                            prev->next = walker->next;
                        else
                            walkerDpy->rootSync = walker->next;
                        // Deleting a GL sync object needs a current GL context.
                        if (glDeleteSync_PTR && walker->glSync && g_localStorage.currentCtx != EGL_NO_CONTEXT_IMPL)
                            glDeleteSync_PTR(walker->glSync);
                        delete walker;
                        g_localStorage.error = EGL_SUCCESS;
                        return EGL_TRUE;
                    }
                    prev   = walker;
                    walker = walker->next;
                }
                g_localStorage.error = EGL_BAD_PARAMETER;
                return EGL_FALSE;
            }
            walkerDpy = walkerDpy->next;
        }
        g_localStorage.error = EGL_BAD_DISPLAY;
        return EGL_FALSE;
    }

    EGLint _eglClientWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags, EGLTime timeout)
    {
        // EGL 1.5 §3.8.2: EGL_SYNC_FLUSH_COMMANDS_BIT is the only defined bit.
        if (flags & ~EGL_SYNC_FLUSH_COMMANDS_BIT)
        {
            g_localStorage.error = EGL_BAD_PARAMETER;
            return EGL_FALSE;
        }
        auto            _rl       = g_globalStorage.placeRootDpy_readlock();
        EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;
        while (walkerDpy)
        {
            if (reinterpret_cast<EGLDisplay>(walkerDpy) == dpy)
            {
                if (!walkerDpy->initialized)
                {
                    g_localStorage.error = EGL_NOT_INITIALIZED;
                    // EGL 1.5 §3.8.2 requires EGL_FALSE on failure; EGL has no
                    // EGL_WAIT_FAILED token.
                    return EGL_FALSE;
                }
                // eglDestroySync unlinks and deletes under this very mutex, so a
                // reader without it can end up waiting on freed memory.
                std::lock_guard<std::mutex> lk(walkerDpy->mutex);
                EGLSyncImpl*                walkerSync = walkerDpy->rootSync;
                while (walkerSync)
                {
                    if (reinterpret_cast<EGLSync>(walkerSync) == sync)
                    {
                        if (!glClientWaitSync_PTR)
                        {
                            g_localStorage.error = EGL_BAD_MATCH;
                            return EGL_FALSE;
                        }
                        GLbitfield glFlags = (flags & EGL_SYNC_FLUSH_COMMANDS_BIT) ? GL_SYNC_FLUSH_COMMANDS_BIT_GL : 0;
                        GLenum     result  = 0;
                        if (timeout == EGL_FOREVER)
                        {
                            // GL_TIMEOUT_IGNORED has no defined meaning for
                            // glClientWaitSync, so an unbounded wait is built from
                            // finite waits; otherwise EGL_FOREVER could spuriously
                            // report a timeout.
                            const unsigned long long oneSecond = 1000000000ull;
                            do
                            {
                                result  = glClientWaitSync_PTR(walkerSync->glSync, glFlags, oneSecond);
                                glFlags = 0; // flushing once is enough
                            } while (result == GL_TIMEOUT_EXPIRED_GL);
                        }
                        else
                        {
                            result = glClientWaitSync_PTR(walkerSync->glSync, glFlags, (unsigned long long)timeout);
                        }
                        switch (result)
                        {
                        case GL_ALREADY_SIGNALED_GL:
                        case GL_CONDITION_SATISFIED_GL:
                            g_localStorage.error = EGL_SUCCESS;
                            return EGL_CONDITION_SATISFIED;
                        case GL_TIMEOUT_EXPIRED_GL:
                            g_localStorage.error = EGL_SUCCESS;
                            return EGL_TIMEOUT_EXPIRED;
                        default:
                            g_localStorage.error = EGL_BAD_PARAMETER;
                            return EGL_FALSE;
                        }
                    }
                    walkerSync = walkerSync->next;
                }
                g_localStorage.error = EGL_BAD_PARAMETER;
                return EGL_FALSE;
            }
            walkerDpy = walkerDpy->next;
        }
        g_localStorage.error = EGL_BAD_DISPLAY;
        return EGL_FALSE;
    }

    EGLBoolean _eglGetSyncAttrib(EGLDisplay dpy, EGLSync sync, EGLint attribute, EGLAttrib* value)
    {
        if (!value)
        {
            g_localStorage.error = EGL_BAD_PARAMETER;
            return EGL_FALSE;
        }
        auto            _rl       = g_globalStorage.placeRootDpy_readlock();
        EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;
        while (walkerDpy)
        {
            if (reinterpret_cast<EGLDisplay>(walkerDpy) == dpy)
            {
                if (!walkerDpy->initialized)
                {
                    g_localStorage.error = EGL_NOT_INITIALIZED;
                    return EGL_FALSE;
                }
                // eglDestroySync deletes the node under this mutex.
                std::lock_guard<std::mutex> lk(walkerDpy->mutex);
                EGLSyncImpl* walkerSync = walkerDpy->rootSync;
                while (walkerSync)
                {
                    if (reinterpret_cast<EGLSync>(walkerSync) == sync)
                    {
                        switch (attribute)
                        {
                        case EGL_SYNC_TYPE:
                            *value               = (EGLAttrib)walkerSync->type;
                            g_localStorage.error = EGL_SUCCESS;
                            return EGL_TRUE;
                        case EGL_SYNC_CONDITION:
                            *value               = (EGLAttrib)EGL_SYNC_PRIOR_COMMANDS_COMPLETE;
                            g_localStorage.error = EGL_SUCCESS;
                            return EGL_TRUE;
                        case EGL_SYNC_STATUS:
                        {
                            // Querying a GL sync object needs a current GL context.
                            if (!glGetSynciv_PTR || g_localStorage.currentCtx == EGL_NO_CONTEXT_IMPL)
                            {
                                g_localStorage.error = EGL_BAD_MATCH;
                                return EGL_FALSE;
                            }
                            GLint   status = 0;
                            GLsizei len    = 0;
                            glGetSynciv_PTR(walkerSync->glSync, GL_SYNC_STATUS_GL, 1, &len, &status);
                            *value               = (status == GL_SIGNALED_GL) ? EGL_SIGNALED : EGL_UNSIGNALED;
                            g_localStorage.error = EGL_SUCCESS;
                            return EGL_TRUE;
                        }
                        default:
                            g_localStorage.error = EGL_BAD_ATTRIBUTE;
                            return EGL_FALSE;
                        }
                    }
                    walkerSync = walkerSync->next;
                }
                g_localStorage.error = EGL_BAD_PARAMETER;
                return EGL_FALSE;
            }
            walkerDpy = walkerDpy->next;
        }
        g_localStorage.error = EGL_BAD_DISPLAY;
        return EGL_FALSE;
    }

    EGLBoolean _eglWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags)
    {
        if (flags != 0)
        {
            g_localStorage.error = EGL_BAD_PARAMETER;
            return EGL_FALSE;
        }
        // EGL 1.5 §3.8.4: EGL_BAD_MATCH if no current context.
        if (g_localStorage.currentCtx == EGL_NO_CONTEXT_IMPL)
        {
            g_localStorage.error = EGL_BAD_MATCH;
            return EGL_FALSE;
        }
        auto            _rl       = g_globalStorage.placeRootDpy_readlock();
        EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;
        while (walkerDpy)
        {
            if (reinterpret_cast<EGLDisplay>(walkerDpy) == dpy)
            {
                if (!walkerDpy->initialized)
                {
                    g_localStorage.error = EGL_NOT_INITIALIZED;
                    return EGL_FALSE;
                }
                // eglDestroySync deletes the node under this mutex.
                std::lock_guard<std::mutex> lk(walkerDpy->mutex);
                EGLSyncImpl* walkerSync = walkerDpy->rootSync;
                while (walkerSync)
                {
                    if (reinterpret_cast<EGLSync>(walkerSync) == sync)
                    {
                        if (!glWaitSync_PTR)
                        {
                            g_localStorage.error = EGL_BAD_MATCH;
                            return EGL_FALSE;
                        }
                        glWaitSync_PTR(walkerSync->glSync, 0, GL_TIMEOUT_IGNORED_GL);
                        g_localStorage.error = EGL_SUCCESS;
                        return EGL_TRUE;
                    }
                    walkerSync = walkerSync->next;
                }
                g_localStorage.error = EGL_BAD_PARAMETER;
                return EGL_FALSE;
            }
            walkerDpy = walkerDpy->next;
        }
        g_localStorage.error = EGL_BAD_DISPLAY;
        return EGL_FALSE;
    }

} // extern "C"
