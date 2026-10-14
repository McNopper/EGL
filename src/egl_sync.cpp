#include "egl_common.h"
#include <new>
#include <vector>
#include <chrono>

extern "C"
{

    // Returns the sync node for `sync`, or nullptr when the handle is unknown.
    // No error is set - the not-found code stays at the call site. The caller
    // holds the display mutex: eglDestroySync unlinks and deletes under it.
    EGLSyncImpl* _eglFindSync(EGLDisplayImpl* walkerDpy, EGLSync sync)
    {
        EGLSyncImpl* walkerSync = walkerDpy->rootSync;

        while (walkerSync)
        {
            if (reinterpret_cast<EGLSync>(walkerSync) == sync)
            {
                return walkerSync;
            }

            walkerSync = walkerSync->next;
        }

        return nullptr;
    }

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
        EGLDisplayImpl* walkerDpy = _eglFindDisplay(dpy);
        if (!walkerDpy)
        {
            return EGL_NO_SYNC;
        }
        if (!walkerDpy->initialized)
        {
            g_localStorage.error = EGL_NOT_INITIALIZED;
            return EGL_NO_SYNC;
        }
        // EGL 1.5 §3.8.1: EGL_BAD_PARAMETER if type is not a recognised type
        // (EGL_BAD_ATTRIBUTE is for unrecognised attribute names/values).
        // EGL_KHR_reusable_sync adds EGL_SYNC_REUSABLE_KHR as a supported
        // type, and the core entry must accept extension types while the
        // extension is advertised ("a supported type" has no entry-point
        // affinity). A reusable sync is pure CPU state: no current context
        // and no GL object are involved in creating one.
        if (type == EGL_SYNC_REUSABLE_KHR)
        {
            EGLSyncImpl* newSync = new (std::nothrow) EGLSyncImpl();

            if (!newSync)
            {
                g_localStorage.error = EGL_BAD_ALLOC;
                return EGL_NO_SYNC;
            }

            newSync->type     = EGL_SYNC_REUSABLE_KHR;
            newSync->glSync   = nullptr;
            newSync->ctxList  = nullptr;
            newSync->reusable = std::make_shared<EGLReusableSyncState>();

            std::lock_guard<std::mutex> lk(walkerDpy->mutex);

            newSync->next     = walkerDpy->rootSync;
            walkerDpy->rootSync = newSync;

            g_localStorage.error = EGL_SUCCESS;
            return reinterpret_cast<EGLSync>(newSync);
        }

        if (type != EGL_SYNC_FENCE)
        {
            g_localStorage.error = EGL_BAD_PARAMETER;
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
        newSync->ctxList = _eglFindContextList(g_localStorage.currentCtx, g_localStorage.currentDraw);
        newSync->next       = walkerDpy->rootSync;
        walkerDpy->rootSync = newSync;
        g_localStorage.error = EGL_SUCCESS;
        return reinterpret_cast<EGLSync>(newSync);
    }

    EGLBoolean _eglDestroySync(EGLDisplay dpy, EGLSync sync)
    {
        auto            _rl       = g_globalStorage.placeRootDpy_readlock();
        EGLDisplayImpl* walkerDpy = _eglFindDisplay(dpy);
        if (!walkerDpy)
        {
            return EGL_FALSE;
        }
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
                // EGL_KHR_reusable_sync: destroying a reusable sync wakes
                // every blocked eglClientWaitSync, "as if it had been
                // signaled"; a waiter still inside the wait keeps the shared
                // state alive until it returns.
                if (walker->type == EGL_SYNC_REUSABLE_KHR && walker->reusable)
                {
                    std::lock_guard<std::mutex> slk(walker->reusable->mutex);
                    walker->reusable->destroyed = true;
                    walker->reusable->wake.notify_all();
                }
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

    EGLint _eglClientWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags, EGLTime timeout)
    {
        // EGL 1.5 §3.8.2: EGL_SYNC_FLUSH_COMMANDS_BIT is the only defined bit.
        if (flags & ~EGL_SYNC_FLUSH_COMMANDS_BIT)
        {
            g_localStorage.error = EGL_BAD_PARAMETER;
            return EGL_FALSE;
        }
        auto            _rl       = g_globalStorage.placeRootDpy_readlock();
        EGLDisplayImpl* walkerDpy = _eglFindDisplay(dpy);
        if (!walkerDpy)
        {
            return EGL_FALSE;
        }
        if (!walkerDpy->initialized)
        {
            g_localStorage.error = EGL_NOT_INITIALIZED;
            // EGL 1.5 §3.8.2 requires EGL_FALSE on failure; EGL has no
            // EGL_WAIT_FAILED token.
            return EGL_FALSE;
        }
        // eglDestroySync unlinks and deletes under this very mutex, so a
        // reader without it can end up waiting on freed memory. The reusable
        // path then blocks WITHOUT the display mutex (eglSignalSyncKHR and
        // eglDestroySync on other threads need it): it copies the shared
        // blocking state out under the lock, and that state's lifetime is
        // independent of the sync object a waiter may have destroyed.
        std::shared_ptr<EGLReusableSyncState> reusable;

        {
            std::lock_guard<std::mutex> lk(walkerDpy->mutex);
            EGLSyncImpl*                walkerSync = _eglFindSync(walkerDpy, sync);
            if (!walkerSync)
            {
                g_localStorage.error = EGL_BAD_PARAMETER;
                return EGL_FALSE;
            }

            if (walkerSync->type == EGL_SYNC_REUSABLE_KHR)
            {
                reusable = walkerSync->reusable;
            }
            else
            {
                if (!glClientWaitSync_PTR)
                {
                    g_localStorage.error = EGL_BAD_MATCH;
                    return EGL_FALSE;
                }

                bool borrowed = false;

                // EGL 1.5 section 3.8.1.3: the wait is valid without a current
                // context - only the flush bit is ignored then. The fence belongs to
                // the binding that created it, so borrow that binding through the
                // platform hook and release it afterwards, leaving the thread as it
                // was.
                if (g_localStorage.currentCtx == EGL_NO_CONTEXT_IMPL && walkerSync->ctxList)
                {
                    if (__makeCurrent(walkerDpy, &walkerSync->ctxList->surface->nativeSurfaceContainer, &walkerSync->ctxList->nativeContextContainer))
                    {
                        borrowed = true;
                    }
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

                if (borrowed)
                {
                    // Restore the context-free state the caller had.
                    __makeCurrent(walkerDpy, nullptr, nullptr);
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
        }

        // EGL_KHR_reusable_sync: block on the shared state, outside the
        // display mutex.
        bool wasSignaled;
        {
            std::lock_guard<std::mutex> slk(reusable->mutex);
            wasSignaled = reusable->signaled;
        }

        if ((flags & EGL_SYNC_FLUSH_COMMANDS_BIT) && !wasSignaled)
        {
            // "the equivalent of Flush() ... before blocking on <sync>":
            // eglWaitClient dispatches to whichever stack is current and
            // takes the display mutex itself, so it runs unlocked here.
            _eglWaitClient();
        }

        std::unique_lock<std::mutex> slk(reusable->mutex);
        auto                         stillBlocked = [&reusable]() { return reusable->signaled || reusable->destroyed; };

        bool satisfied = false;

        if (timeout == 0)
        {
            // A zero timeout only tests the current status.
            satisfied = reusable->signaled;
        }
        else if (timeout == EGL_FOREVER)
        {
            reusable->wake.wait(slk, stillBlocked);
            // Destroying the sync while blocked wakes the wait "as if it had
            // been signaled".
            satisfied = true;
        }
        else
        {
            // Clamped to what chrono's signed nanosecond duration represents;
            // larger timeouts are effectively EGL_FOREVER anyway.
            const unsigned long long maxNs = 0x7FFFFFFFFFFFFFFFull;
            satisfied = reusable->wake.wait_until(
                slk,
                std::chrono::steady_clock::now() + std::chrono::nanoseconds((long long)(timeout > maxNs ? maxNs : timeout)),
                stillBlocked);
        }

        g_localStorage.error = EGL_SUCCESS;
        return satisfied ? EGL_CONDITION_SATISFIED : EGL_TIMEOUT_EXPIRED;
    }

    EGLBoolean _eglGetSyncAttrib(EGLDisplay dpy, EGLSync sync, EGLint attribute, EGLAttrib* value)
    {
        if (!value)
        {
            g_localStorage.error = EGL_BAD_PARAMETER;
            return EGL_FALSE;
        }
        auto            _rl       = g_globalStorage.placeRootDpy_readlock();
        EGLDisplayImpl* walkerDpy = _eglFindDisplay(dpy);
        if (!walkerDpy)
        {
            return EGL_FALSE;
        }
        if (!walkerDpy->initialized)
        {
            g_localStorage.error = EGL_NOT_INITIALIZED;
            return EGL_FALSE;
        }
        // eglDestroySync deletes the node under this mutex.
        std::lock_guard<std::mutex> lk(walkerDpy->mutex);
        EGLSyncImpl* walkerSync = _eglFindSync(walkerDpy, sync);
        if (!walkerSync)
        {
            g_localStorage.error = EGL_BAD_PARAMETER;
            return EGL_FALSE;
        }
        switch (attribute)
        {
        case EGL_SYNC_TYPE:
            *value               = (EGLAttrib)walkerSync->type;
            g_localStorage.error = EGL_SUCCESS;
            return EGL_TRUE;
        case EGL_SYNC_CONDITION:
            // EGL_KHR_reusable_sync table 3.cc: only EGL_SYNC_TYPE and
            // EGL_SYNC_STATUS are queryable on a reusable sync.
            if (walkerSync->type != EGL_SYNC_FENCE)
            {
                g_localStorage.error = EGL_BAD_MATCH;
                return EGL_FALSE;
            }
            *value               = (EGLAttrib)EGL_SYNC_PRIOR_COMMANDS_COMPLETE;
            g_localStorage.error = EGL_SUCCESS;
            return EGL_TRUE;
        case EGL_SYNC_STATUS:
        {
            // A reusable sync's status is plain shared state - no GL query
            // and no current context involved.
            if (walkerSync->type == EGL_SYNC_REUSABLE_KHR && walkerSync->reusable)
            {
                std::lock_guard<std::mutex> slk(walkerSync->reusable->mutex);
                *value               = walkerSync->reusable->signaled ? (EGLAttrib)EGL_SIGNALED : (EGLAttrib)EGL_UNSIGNALED;
                g_localStorage.error = EGL_SUCCESS;
                return EGL_TRUE;
            }
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
        EGLDisplayImpl* walkerDpy = _eglFindDisplay(dpy);
        if (!walkerDpy)
        {
            return EGL_FALSE;
        }
        if (!walkerDpy->initialized)
        {
            g_localStorage.error = EGL_NOT_INITIALIZED;
            return EGL_FALSE;
        }
        // eglDestroySync deletes the node under this mutex.
        std::lock_guard<std::mutex> lk(walkerDpy->mutex);
        EGLSyncImpl* walkerSync = _eglFindSync(walkerDpy, sync);
        if (!walkerSync)
        {
            g_localStorage.error = EGL_BAD_PARAMETER;
            return EGL_FALSE;
        }
        // The server wait is a GL fence wait; a reusable sync has no GL
        // object behind it (EGL_KHR_reusable_sync has no eglWaitSync entry).
        if (walkerSync->type != EGL_SYNC_FENCE || !walkerSync->glSync)
        {
            g_localStorage.error = EGL_BAD_MATCH;
            return EGL_FALSE;
        }
        if (!glWaitSync_PTR)
        {
            g_localStorage.error = EGL_BAD_MATCH;
            return EGL_FALSE;
        }
        glWaitSync_PTR(walkerSync->glSync, 0, GL_TIMEOUT_IGNORED_GL);
        g_localStorage.error = EGL_SUCCESS;
        return EGL_TRUE;
    }

    // EGL_KHR_fence_sync and EGL_KHR_wait_sync: the KHR entry points predate
    // EGL 1.5 and pass the attribute list and the queried values as EGLint
    // instead of EGLAttrib; they are otherwise identical to the core sync
    // functions, which carry the whole validation.
    EGLSyncKHR _eglCreateSyncKHR(EGLDisplay dpy, EGLenum type, const EGLint* attrib_list)
    {
        // The extension specifies EGL_BAD_ATTRIBUTE for a type neither it nor
        // the core defines; the core 1.5 function says EGL_BAD_PARAMETER (the
        // error was deliberately changed for the core, spec change log Bug
        // 11963). EGL_SYNC_REUSABLE_KHR passes through: the core create is
        // the single validation path and accepts it while the extension is
        // advertised.
        if (type != EGL_SYNC_FENCE_KHR && type != EGL_SYNC_REUSABLE_KHR)
        {
            g_localStorage.error = EGL_BAD_ATTRIBUTE;
            return EGL_NO_SYNC_KHR;
        }

        std::vector<EGLAttrib> converted;

        if (attrib_list)
        {
            for (const EGLint* walker = attrib_list; walker[0] != EGL_NONE; walker += 2)
            {
                converted.push_back(static_cast<EGLAttrib>(walker[0]));
                converted.push_back(static_cast<EGLAttrib>(walker[1]));
            }
        }

        converted.push_back(EGL_NONE);

        return _eglCreateSync(dpy, type, converted.data());
    }

    EGLBoolean _eglDestroySyncKHR(EGLDisplay dpy, EGLSyncKHR sync)
    {
        return _eglDestroySync(dpy, sync);
    }

    // EGL_KHR_reusable_sync: transitions the status of a reusable sync and
    // releases every eglClientWaitSync blocked on an unsignaled-to-signaled
    // transition.
    EGLBoolean _eglSignalSyncKHR(EGLDisplay dpy, EGLSyncKHR sync, EGLenum mode)
    {
        auto            _rl       = g_globalStorage.placeRootDpy_readlock();
        EGLDisplayImpl* walkerDpy = _eglFindDisplay(dpy);

        if (!walkerDpy)
        {
            return EGL_FALSE;
        }

        std::lock_guard<std::mutex> lk(walkerDpy->mutex);

        if (!walkerDpy->initialized)
        {
            g_localStorage.error = EGL_NOT_INITIALIZED;
            return EGL_FALSE;
        }

        EGLSyncImpl* walkerSync = _eglFindSync(walkerDpy, sync);

        if (!walkerSync)
        {
            g_localStorage.error = EGL_BAD_PARAMETER;
            return EGL_FALSE;
        }

        if (walkerSync->type != EGL_SYNC_REUSABLE_KHR)
        {
            g_localStorage.error = EGL_BAD_MATCH;
            return EGL_FALSE;
        }

        if (mode != EGL_SIGNALED_KHR && mode != EGL_UNSIGNALED_KHR)
        {
            g_localStorage.error = EGL_BAD_ATTRIBUTE;
            return EGL_FALSE;
        }

        {
            std::lock_guard<std::mutex> slk(walkerSync->reusable->mutex);
            const bool wasSignaled = walkerSync->reusable->signaled;

            walkerSync->reusable->signaled = (mode == EGL_SIGNALED_KHR);

            if (!wasSignaled && walkerSync->reusable->signaled)
            {
                walkerSync->reusable->wake.notify_all();
            }
        }

        g_localStorage.error = EGL_SUCCESS;
        return EGL_TRUE;
    }

    EGLint _eglClientWaitSyncKHR(EGLDisplay dpy, EGLSyncKHR sync, EGLint flags, EGLTimeKHR timeout)
    {
        return _eglClientWaitSync(dpy, sync, flags, timeout);
    }

    EGLBoolean _eglGetSyncAttribKHR(EGLDisplay dpy, EGLSyncKHR sync, EGLint attribute, EGLint* value)
    {
        EGLAttrib value64 = 0;

        if (!_eglGetSyncAttrib(dpy, sync, attribute, &value64))
        {
            return EGL_FALSE;
        }

        if (value)
        {
            *value = static_cast<EGLint>(value64);
        }

        return EGL_TRUE;
    }

    EGLint _eglWaitSyncKHR(EGLDisplay dpy, EGLSyncKHR sync, EGLint flags)
    {
        return _eglWaitSync(dpy, sync, flags) ? EGL_TRUE : EGL_FALSE;
    }

} // extern "C"
