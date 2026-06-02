#include "egl_common.h"

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
                EGLSyncImpl* newSync = new EGLSyncImpl();
                if (!newSync)
                {
                    glDeleteSync_PTR(glSync);
                    g_localStorage.error = EGL_BAD_ALLOC;
                    return EGL_NO_SYNC;
                }
                newSync->type   = EGL_SYNC_FENCE;
                newSync->glSync = glSync;
                std::lock_guard<std::mutex> lk(walkerDpy->mutex);
                newSync->next       = walkerDpy->rootSync;
                walkerDpy->rootSync = newSync;
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
                        if (glDeleteSync_PTR)
                            glDeleteSync_PTR(walker->glSync);
                        delete walker;
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
        auto            _rl       = g_globalStorage.placeRootDpy_readlock();
        EGLDisplayImpl* walkerDpy = g_globalStorage.rootDpy;
        while (walkerDpy)
        {
            if (reinterpret_cast<EGLDisplay>(walkerDpy) == dpy)
            {
                if (!walkerDpy->initialized)
                {
                    g_localStorage.error = EGL_NOT_INITIALIZED;
                    return EGL_WAIT_FAILED;
                }
                EGLSyncImpl* walkerSync = walkerDpy->rootSync;
                while (walkerSync)
                {
                    if (reinterpret_cast<EGLSync>(walkerSync) == sync)
                    {
                        if (!glClientWaitSync_PTR)
                        {
                            g_localStorage.error = EGL_BAD_MATCH;
                            return EGL_WAIT_FAILED;
                        }
                        GLbitfield         glFlags   = (flags & EGL_SYNC_FLUSH_COMMANDS_BIT) ? GL_SYNC_FLUSH_COMMANDS_BIT_GL : 0;
                        unsigned long long glTimeout = (timeout == EGL_FOREVER) ? GL_TIMEOUT_IGNORED_GL : (unsigned long long)timeout;
                        GLenum             result    = glClientWaitSync_PTR(walkerSync->glSync, glFlags, glTimeout);
                        switch (result)
                        {
                        case GL_ALREADY_SIGNALED_GL:
                        case GL_CONDITION_SATISFIED_GL:
                            return EGL_CONDITION_SATISFIED;
                        case GL_TIMEOUT_EXPIRED_GL:
                            return EGL_TIMEOUT_EXPIRED;
                        default:
                            g_localStorage.error = EGL_BAD_PARAMETER;
                            return EGL_WAIT_FAILED;
                        }
                    }
                    walkerSync = walkerSync->next;
                }
                g_localStorage.error = EGL_BAD_PARAMETER;
                return EGL_WAIT_FAILED;
            }
            walkerDpy = walkerDpy->next;
        }
        g_localStorage.error = EGL_BAD_DISPLAY;
        return EGL_WAIT_FAILED;
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
                EGLSyncImpl* walkerSync = walkerDpy->rootSync;
                while (walkerSync)
                {
                    if (reinterpret_cast<EGLSync>(walkerSync) == sync)
                    {
                        switch (attribute)
                        {
                        case EGL_SYNC_TYPE:
                            *value = (EGLAttrib)walkerSync->type;
                            return EGL_TRUE;
                        case EGL_SYNC_CONDITION:
                            *value = (EGLAttrib)EGL_SYNC_PRIOR_COMMANDS_COMPLETE;
                            return EGL_TRUE;
                        case EGL_SYNC_STATUS:
                        {
                            if (!glGetSynciv_PTR)
                            {
                                g_localStorage.error = EGL_BAD_MATCH;
                                return EGL_FALSE;
                            }
                            GLint   status = 0;
                            GLsizei len    = 0;
                            glGetSynciv_PTR(walkerSync->glSync, GL_SYNC_STATUS_GL, 1, &len, &status);
                            *value = (status == GL_SIGNALED_GL) ? EGL_SIGNALED : EGL_UNSIGNALED;
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
