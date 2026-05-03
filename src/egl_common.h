#pragma once

#include <atomic>
#include <thread>
#include <mutex>
#include "egl_internal.h"
#include <EGL/eglext.h>

// GL_ARB_sync constants and function pointer externs (defined in platform .cpp files).
#define GL_SYNC_GPU_COMMANDS_COMPLETE     0x9117
#define GL_SYNC_STATUS_GL                 0x9114
#define GL_SYNC_CONDITION_GL              0x9113
#define GL_SIGNALED_GL                    0x9119
#define GL_UNSIGNALED_GL                  0x9118
#define GL_ALREADY_SIGNALED_GL            0x911A
#define GL_TIMEOUT_EXPIRED_GL             0x911B
#define GL_CONDITION_SATISFIED_GL         0x911C
#define GL_WAIT_FAILED_GL                 0x911D
#define GL_SYNC_FLUSH_COMMANDS_BIT_GL     0x00000001
#define GL_TIMEOUT_IGNORED_GL             0xFFFFFFFFFFFFFFFFull

// Internal token used by our eglClientWaitSync implementation to signal a wait failure.
// EGL 1.5 reserves 0x30F4 but does not expose it as a public define; it's referenced as
// EGL_WAIT_FAILED_KHR by EGL_KHR_reusable_sync. Defined here to keep <EGL/egl.h> unmodified.
#ifndef EGL_WAIT_FAILED
#define EGL_WAIT_FAILED                   0x30F4
#endif

typedef void(*__PFN_glFinish)();
typedef void* (*__PFN_glFenceSync)(GLenum condition, GLbitfield flags);
typedef void  (*__PFN_glDeleteSync)(void* sync);
typedef GLenum(*__PFN_glClientWaitSync)(void* sync, GLbitfield flags, unsigned long long timeout);
typedef void  (*__PFN_glWaitSync)(void* sync, GLbitfield flags, unsigned long long timeout);
typedef void  (*__PFN_glGetSynciv)(void* sync, GLenum pname, GLsizei count, GLsizei* length, GLint* values);

extern __PFN_glFenceSync glFenceSync_PTR;
extern __PFN_glDeleteSync glDeleteSync_PTR;
extern __PFN_glClientWaitSync glClientWaitSync_PTR;
extern __PFN_glWaitSync glWaitSync_PTR;
extern __PFN_glGetSynciv glGetSynciv_PTR;

#define EGL_NO_SURFACE_IMPL static_cast<EGLSurfaceImpl*>(EGL_NO_SURFACE)
#define EGL_NO_CONTEXT_IMPL static_cast<EGLContextImpl*>(EGL_NO_CONTEXT)

struct GlobalStorage
{
    EGLDisplayImpl* rootDpy = nullptr;

    void rootDpy_readacq()
    {
        lock_read(lock_dpy);
    }
    void rootDpy_writeacq()
    {
        lock_write(lock_dpy);
    }
    void rootDpy_readrel()
    {
        unlock_read(lock_dpy);
    }
    void rootDpy_writerel()
    {
        unlock_write(lock_dpy);
    }

    auto dummy_read()
    {
        lock_read(lock_dummy);
        auto d = dummy;
        unlock_read(lock_dummy);
        return d;
    }
    void dummy_write(NativeLocalStorageContainer d)
    {
        lock_write(lock_dummy);
        dummy = d;
        unlock_write(lock_dummy);
    }

    struct ReadLock
    {
        explicit ReadLock(GlobalStorage* gs) : parent(gs)
        {
            parent->rootDpy_readacq();
        }
        ~ReadLock()
        {
            parent->rootDpy_readrel();
        }

        GlobalStorage* parent;
    };
    struct WriteLock
    {
        explicit WriteLock(GlobalStorage* gs) : parent(gs)
        {
            parent->rootDpy_writeacq();
        }
        ~WriteLock()
        {
            parent->rootDpy_writerel();
        }

        GlobalStorage* parent;
    };

    ReadLock  placeRootDpy_readlock() { return ReadLock(this); }
    WriteLock placeRootDpy_writelock() { return WriteLock(this); }

    GlobalStorage()
    {
        memset(&dummy, 0, sizeof(dummy));
    }

private:
    NativeLocalStorageContainer dummy;

    std::atomic_uint32_t lock_dpy = 0u;
    std::atomic_uint32_t lock_dummy = 0u;

    static void lock_read(std::atomic_uint32_t& c)
    {
        if (++c > LOCK_WRITE_VALUE)
        {
            while (c >= LOCK_WRITE_VALUE)
                std::this_thread::yield();
        }
    }
    static void unlock_read(std::atomic_uint32_t& c)
    {
        --c;
    }
    static void lock_write(std::atomic_uint32_t& c)
    {
        uint32_t expected = 0u;
        while (!c.compare_exchange_strong(expected, LOCK_WRITE_VALUE))
        {
            expected = 0u;
            std::this_thread::yield();
        }
    }
    static void unlock_write(std::atomic_uint32_t& c)
    {
        c -= LOCK_WRITE_VALUE;
    }

    constexpr inline static uint32_t LOCK_WRITE_VALUE = 0xdeadbeefu;
};

typedef std::lock_guard<std::mutex> guard_t;

extern thread_local LocalStorage g_localStorage;
extern GlobalStorage g_globalStorage;
extern EGLint g_GL_max_supported_version[2];
extern EGLint g_ES_max_supported_version[2];

extern __PFN_glFinish glFinish_PTR;
#define glFinish(...) glFinish_PTR(__VA_ARGS__)

// Forward declarations of internal lifecycle functions (C++ linkage)
EGLBoolean _eglInternalInit();
void _eglInternalTerminate();
void _eglInternalCleanup();
