/*
    * dialogs.cpp
    * Reboot and shutdown confirmation dialogs
    * Copyright (c) 2026 Daniel Hammer
*/

#include "desktop_internal.hpp"

// ============================================================================
// Reboot Dialog
// ============================================================================

struct RebootDialogState {
    DesktopState* ds;
    // Button layout (pixel-buffer-relative coordinates)
    int btn_w, btn_h, btn_y, reboot_x, cancel_x;
    bool hover_reboot, hover_cancel;
};

static void reboot_dialog_on_draw(Window* win, Framebuffer& fb) {
    RebootDialogState* rs = (RebootDialogState*)win->app_data;
    if (!rs) return;

    Canvas c(win);
    c.fill(colors::WINDOW_BG);

    // "Reboot the system?" centered
    const char* msg = "Reboot the system?";
    int tw = text_width(msg);
    c.text((c.w - tw) / 2, 30, msg, colors::TEXT_COLOR);

    // Compute button layout
    int btn_w = 100;
    int btn_h = 32;
    int btn_y = c.h - btn_h - 20;
    int gap = 20;
    int total_w = btn_w * 2 + gap;
    int bx = (c.w - total_w) / 2;
    rs->btn_w = btn_w;
    rs->btn_h = btn_h;
    rs->btn_y = btn_y;
    rs->reboot_x = bx;
    rs->cancel_x = bx + btn_w + gap;

    // Draw Reboot button
    Color reboot_bg = rs->hover_reboot
        ? Color::from_rgb(0xDD, 0x44, 0x44)
        : Color::from_rgb(0xCC, 0x33, 0x33);
    c.button(rs->reboot_x, btn_y, btn_w, btn_h, "Reboot", reboot_bg, colors::WHITE, 4);

    // Draw Cancel button
    Color cancel_bg = rs->hover_cancel
        ? Color::from_rgb(0x99, 0x99, 0x99)
        : Color::from_rgb(0x88, 0x88, 0x88);
    c.button(rs->cancel_x, btn_y, btn_w, btn_h, "Cancel", cancel_bg, colors::WHITE, 4);
}

static void reboot_dialog_on_mouse(Window* win, MouseEvent& ev) {
    RebootDialogState* rs = (RebootDialogState*)win->app_data;
    if (!rs) return;

    Rect cr = win->content_rect();
    int lx = ev.x - cr.x;
    int ly = ev.y - cr.y;

    Rect rb = {rs->reboot_x, rs->btn_y, rs->btn_w, rs->btn_h};
    Rect cb = {rs->cancel_x, rs->btn_y, rs->btn_w, rs->btn_h};
    rs->hover_reboot = rb.contains(lx, ly);
    rs->hover_cancel = cb.contains(lx, ly);

    if (ev.left_pressed()) {
        if (rs->hover_reboot) montauk::reset();
        if (rs->hover_cancel) {
            for (int i = 0; i < rs->ds->window_count; i++) {
                if (rs->ds->windows[i].app_data == rs) {
                    desktop_close_window(rs->ds, i);
                    return;
                }
            }
        }
    }
}

static void reboot_dialog_on_key(Window* win, const Montauk::KeyEvent& key) {
    RebootDialogState* rs = (RebootDialogState*)win->app_data;
    if (!rs || !key.pressed) return;

    if (key.ascii == '\n' || key.ascii == '\r') {
        montauk::reset();
    }
    if (key.scancode == 0x01) { // Escape
        for (int i = 0; i < rs->ds->window_count; i++) {
            if (rs->ds->windows[i].app_data == rs) {
                desktop_close_window(rs->ds, i);
                return;
            }
        }
    }
}

static void reboot_dialog_on_close(Window* win) {
    if (win->app_data) {
        montauk::mfree(win->app_data);
        win->app_data = nullptr;
    }
}

void open_reboot_dialog(DesktopState* ds) {
    int wx = (ds->screen_w - 300) / 2;
    int wy = (ds->screen_h - 150) / 2;
    int idx = desktop_create_window(ds, "Reboot", wx, wy, 300, 150);
    if (idx < 0) return;

    Window* win = &ds->windows[idx];
    RebootDialogState* rs = (RebootDialogState*)montauk::malloc(sizeof(RebootDialogState));
    montauk::memset(rs, 0, sizeof(RebootDialogState));
    rs->ds = ds;

    win->app_data = rs;
    win->on_draw = reboot_dialog_on_draw;
    win->on_mouse = reboot_dialog_on_mouse;
    win->on_key = reboot_dialog_on_key;
    win->on_close = reboot_dialog_on_close;
}

// ============================================================================
// Shutdown Dialog
// ============================================================================

struct ShutdownDialogState {
    DesktopState* ds;
    int btn_w, btn_h, btn_y, shutdown_x, cancel_x;
    bool hover_shutdown, hover_cancel;
};

static void shutdown_dialog_on_draw(Window* win, Framebuffer& fb) {
    ShutdownDialogState* ss = (ShutdownDialogState*)win->app_data;
    if (!ss) return;

    Canvas c(win);
    c.fill(colors::WINDOW_BG);

    const char* msg = "Shut down the system?";
    int tw = text_width(msg);
    c.text((c.w - tw) / 2, 30, msg, colors::TEXT_COLOR);

    int btn_w = 100;
    int btn_h = 32;
    int btn_y = c.h - btn_h - 20;
    int gap = 20;
    int total_w = btn_w * 2 + gap;
    int bx = (c.w - total_w) / 2;
    ss->btn_w = btn_w;
    ss->btn_h = btn_h;
    ss->btn_y = btn_y;
    ss->shutdown_x = bx;
    ss->cancel_x = bx + btn_w + gap;

    Color shutdown_bg = ss->hover_shutdown
        ? Color::from_rgb(0xDD, 0x44, 0x44)
        : Color::from_rgb(0xCC, 0x33, 0x33);
    c.button(ss->shutdown_x, btn_y, btn_w, btn_h, "Shut Down", shutdown_bg, colors::WHITE, 4);

    Color cancel_bg = ss->hover_cancel
        ? Color::from_rgb(0x99, 0x99, 0x99)
        : Color::from_rgb(0x88, 0x88, 0x88);
    c.button(ss->cancel_x, btn_y, btn_w, btn_h, "Cancel", cancel_bg, colors::WHITE, 4);
}

static void shutdown_dialog_on_mouse(Window* win, MouseEvent& ev) {
    ShutdownDialogState* ss = (ShutdownDialogState*)win->app_data;
    if (!ss) return;

    Rect cr = win->content_rect();
    int lx = ev.x - cr.x;
    int ly = ev.y - cr.y;

    Rect sb = {ss->shutdown_x, ss->btn_y, ss->btn_w, ss->btn_h};
    Rect cb = {ss->cancel_x, ss->btn_y, ss->btn_w, ss->btn_h};
    ss->hover_shutdown = sb.contains(lx, ly);
    ss->hover_cancel = cb.contains(lx, ly);

    if (ev.left_pressed()) {
        if (ss->hover_shutdown) montauk::shutdown();
        if (ss->hover_cancel) {
            for (int i = 0; i < ss->ds->window_count; i++) {
                if (ss->ds->windows[i].app_data == ss) {
                    desktop_close_window(ss->ds, i);
                    return;
                }
            }
        }
    }
}

static void shutdown_dialog_on_key(Window* win, const Montauk::KeyEvent& key) {
    ShutdownDialogState* ss = (ShutdownDialogState*)win->app_data;
    if (!ss || !key.pressed) return;

    if (key.ascii == '\n' || key.ascii == '\r') {
        montauk::shutdown();
    }
    if (key.scancode == 0x01) { // Escape
        for (int i = 0; i < ss->ds->window_count; i++) {
            if (ss->ds->windows[i].app_data == ss) {
                desktop_close_window(ss->ds, i);
                return;
            }
        }
    }
}

static void shutdown_dialog_on_close(Window* win) {
    if (win->app_data) {
        montauk::mfree(win->app_data);
        win->app_data = nullptr;
    }
}

void open_shutdown_dialog(DesktopState* ds) {
    int wx = (ds->screen_w - 300) / 2;
    int wy = (ds->screen_h - 150) / 2;
    int idx = desktop_create_window(ds, "Shut Down", wx, wy, 300, 150);
    if (idx < 0) return;

    Window* win = &ds->windows[idx];
    ShutdownDialogState* ss = (ShutdownDialogState*)montauk::malloc(sizeof(ShutdownDialogState));
    montauk::memset(ss, 0, sizeof(ShutdownDialogState));
    ss->ds = ds;

    win->app_data = ss;
    win->on_draw = shutdown_dialog_on_draw;
    win->on_mouse = shutdown_dialog_on_mouse;
    win->on_key = shutdown_dialog_on_key;
    win->on_close = shutdown_dialog_on_close;
}
