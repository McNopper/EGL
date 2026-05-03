/**
 * Platform window helpers — Wayland implementation (xdg-shell).
 *
 * Implements the common.h interface for Wayland displays.
 * GL/Vulkan work happens inside libEGL; this file only handles window
 * creation and event processing.
 *
 * The MIT License (MIT)
 * Copyright (c) since 2014 Norbert Nopper
 */

#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include "common.h"

// wl_egl_window must exactly match the layout in egl_internal.h (WL_EGL_PLATFORM section).
// ODR is satisfied because both TUs define the same struct with the same member types.
struct wl_egl_window {
    struct wl_surface* surface;
    int                width;
    int                height;
};

// ── Native window state ───────────────────────────────────────────────────────

struct __NativeWindow
{
    struct wl_display*   display;
    struct wl_registry*  registry;
    struct wl_compositor* compositor;
    struct xdg_wm_base*  xdgWmBase;
    struct wl_seat*      seat;
    struct wl_keyboard*  keyboard;

    struct wl_surface*   surface;
    struct xdg_surface*  xdgSurface;
    struct xdg_toplevel* xdgToplevel;

    struct wl_egl_window eglWindow;   // inline; __nativeWindowHandle returns &eglWindow

    bool running;
    bool configured;
};

// ── Forward declarations ──────────────────────────────────────────────────────

static void keyboard_key(void* data, struct wl_keyboard* keyboard,
                          uint32_t serial, uint32_t time, uint32_t key, uint32_t state);

// ── Wayland listener callbacks ────────────────────────────────────────────────

// xdg_wm_base: compositor keepalive (MUST reply to ping or compositor kills the client)
static const xdg_wm_base_listener s_wmBaseListener = {
    [](void*, struct xdg_wm_base* base, uint32_t serial) {
        xdg_wm_base_pong(base, serial);
    }
};

static const xdg_surface_listener s_xdgSurfaceListener = {
    [](void* data, struct xdg_surface* surf, uint32_t serial) {
        auto* win = static_cast<__NativeWindow*>(data);
        xdg_surface_ack_configure(surf, serial);
        win->configured = true;
    }
};

static void s_toplevelConfigure(void* data, struct xdg_toplevel*,
                                  int32_t w, int32_t h, struct wl_array*)
{
    auto* win = static_cast<__NativeWindow*>(data);
    if (w > 0 && h > 0)
    {
        win->eglWindow.width  = w;
        win->eglWindow.height = h;
    }
}

static void s_toplevelClose(void* data, struct xdg_toplevel*)
{
    static_cast<__NativeWindow*>(data)->running = false;
}

// xdg_toplevel_listener only guarantees configure + close (v1).
// configure_bounds (v4) and wm_capabilities (v5) are zero-initialized to nullptr;
// Wayland ignores NULL function pointers for optional protocol events.
static xdg_toplevel_listener s_toplevelListener = {
    s_toplevelConfigure,
    s_toplevelClose,
};

// Keyboard listeners
static const wl_keyboard_listener s_keyboardListener = {
    // keymap
    [](void*, struct wl_keyboard*, uint32_t, int32_t, uint32_t) {},
    // enter
    [](void*, struct wl_keyboard*, uint32_t, struct wl_surface*, struct wl_array*) {},
    // leave
    [](void*, struct wl_keyboard*, uint32_t, struct wl_surface*) {},
    // key — ESC: evdev scancode 1, XKB offset +8 → keycode 9
    keyboard_key,
    // modifiers
    [](void*, struct wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {},
    // repeat_info
    [](void*, struct wl_keyboard*, int32_t, int32_t) {}
};

static void keyboard_key(void* data, struct wl_keyboard*, uint32_t, uint32_t,
                          uint32_t key, uint32_t state)
{
    // state 1 = WL_KEYBOARD_KEY_STATE_PRESSED
    if (state == 1 && key == 1)  // evdev KEY_ESC = 1
        static_cast<__NativeWindow*>(data)->running = false;
}

// Seat capabilities
static const wl_seat_listener s_seatListener = {
    [](void* data, struct wl_seat* seat, uint32_t capabilities) {
        auto* win = static_cast<__NativeWindow*>(data);
        const bool hasKbd = (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;
        if (hasKbd && !win->keyboard)
        {
            win->keyboard = wl_seat_get_keyboard(seat);
            wl_keyboard_add_listener(win->keyboard, &s_keyboardListener, win);
        }
        else if (!hasKbd && win->keyboard)
        {
            wl_keyboard_release(win->keyboard);
            win->keyboard = nullptr;
        }
    },
    // name
    [](void*, struct wl_seat*, const char*) {}
};

// Global registry: bind compositor, xdg_wm_base, seat
struct RegistryGlobals {
    struct wl_compositor* compositor;
    struct xdg_wm_base*   xdgWmBase;
    struct wl_seat*       seat;
};

static const wl_registry_listener s_registryListener = {
    [](void* data, struct wl_registry* reg, uint32_t name,
       const char* iface, uint32_t ver)
    {
        auto* g = static_cast<RegistryGlobals*>(data);
        if (!g->compositor && strcmp(iface, "wl_compositor") == 0)
            g->compositor = static_cast<struct wl_compositor*>(
                wl_registry_bind(reg, name, &wl_compositor_interface, ver < 4 ? ver : 4));
        else if (!g->xdgWmBase && strcmp(iface, "xdg_wm_base") == 0)
            g->xdgWmBase = static_cast<struct xdg_wm_base*>(
                wl_registry_bind(reg, name, &xdg_wm_base_interface, ver < 2 ? ver : 2));
        else if (!g->seat && strcmp(iface, "wl_seat") == 0)
            g->seat = static_cast<struct wl_seat*>(
                wl_registry_bind(reg, name, &wl_seat_interface, ver < 5 ? ver : 5));
    },
    [](void*, struct wl_registry*, uint32_t) {}
};

// ── Public interface ──────────────────────────────────────────────────────────

EGLNativeDisplayType __openDisplay()
{
    struct wl_display* dpy = wl_display_connect(nullptr);
    if (!dpy)
    {
        fprintf(stderr, "wl_display_connect failed: is a Wayland compositor running?\n");
        exit(1);
    }
    return reinterpret_cast<EGLNativeDisplayType>(dpy);
}

__NativeWindow* __createWindow(EGLNativeDisplayType nativeDpy, EGLint /*visual_id*/,
                                int width, int height, const char* title)
{
    struct wl_display* wlDpy = reinterpret_cast<struct wl_display*>(nativeDpy);

    RegistryGlobals globals = {};
    struct wl_registry* reg = wl_display_get_registry(wlDpy);
    wl_registry_add_listener(reg, &s_registryListener, &globals);
    wl_display_roundtrip(wlDpy);  // populate globals

    if (!globals.compositor || !globals.xdgWmBase)
    {
        fprintf(stderr, "Wayland: missing wl_compositor or xdg_wm_base\n");
        wl_registry_destroy(reg);
        return nullptr;
    }

    xdg_wm_base_add_listener(globals.xdgWmBase, &s_wmBaseListener, nullptr);

    auto* win = new __NativeWindow{};
    win->display    = wlDpy;
    win->registry   = reg;
    win->compositor = globals.compositor;
    win->xdgWmBase  = globals.xdgWmBase;
    win->seat       = globals.seat;
    win->running    = true;
    win->configured = false;

    win->surface = wl_compositor_create_surface(win->compositor);
    if (!win->surface)
    {
        delete win;
        return nullptr;
    }

    win->xdgSurface = xdg_wm_base_get_xdg_surface(win->xdgWmBase, win->surface);
    if (!win->xdgSurface)
    {
        wl_surface_destroy(win->surface);
        delete win;
        return nullptr;
    }
    xdg_surface_add_listener(win->xdgSurface, &s_xdgSurfaceListener, win);

    win->xdgToplevel = xdg_surface_get_toplevel(win->xdgSurface);
    if (!win->xdgToplevel)
    {
        xdg_surface_destroy(win->xdgSurface);
        wl_surface_destroy(win->surface);
        delete win;
        return nullptr;
    }
    xdg_toplevel_add_listener(win->xdgToplevel, &s_toplevelListener, win);
    xdg_toplevel_set_title(win->xdgToplevel, title);

    // Bind keyboard if seat is available
    if (win->seat)
    {
        wl_seat_add_listener(win->seat, &s_seatListener, win);
        wl_display_roundtrip(wlDpy);  // trigger seat capability event
    }

    // Commit to trigger initial configure event
    wl_surface_commit(win->surface);
    wl_display_roundtrip(wlDpy);

    // Wait for the initial xdg_surface configure
    while (!win->configured)
        wl_display_dispatch(wlDpy);

    // Initialise inline wl_egl_window
    win->eglWindow.surface = win->surface;
    win->eglWindow.width   = width;
    win->eglWindow.height  = height;

    return win;
}

EGLNativeWindowType __nativeWindowHandle(__NativeWindow* win)
{
    // Return pointer to the inline wl_egl_window — EGL backend reads .surface/.width/.height
    return reinterpret_cast<EGLNativeWindowType>(&win->eglWindow);
}

bool __processEvents(__NativeWindow* win)
{
    if (!win)
        return false;
    wl_display_flush(win->display);
    wl_display_dispatch_pending(win->display);
    return win->running;
}

void __destroyWindow(__NativeWindow* win)
{
    if (!win)
        return;
    if (win->keyboard)    wl_keyboard_release(win->keyboard);
    if (win->xdgToplevel) xdg_toplevel_destroy(win->xdgToplevel);
    if (win->xdgSurface)  xdg_surface_destroy(win->xdgSurface);
    if (win->surface)     wl_surface_destroy(win->surface);
    if (win->xdgWmBase)   xdg_wm_base_destroy(win->xdgWmBase);
    if (win->compositor)  wl_compositor_destroy(win->compositor);
    if (win->seat)        wl_seat_destroy(win->seat);
    if (win->registry)    wl_registry_destroy(win->registry);
    delete win;
}

void __closeDisplay(EGLNativeDisplayType nativeDpy)
{
    if (nativeDpy)
        wl_display_disconnect(reinterpret_cast<struct wl_display*>(nativeDpy));
}

const char* __osName()       { return "Linux"; }
const char* __windowingName() { return "Wayland"; }
const char* __backendName()  { return "VK"; }
