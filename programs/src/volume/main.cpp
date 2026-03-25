/*
 * main.cpp
 * MontaukOS Volume Control
 * Copyright (c) 2026 Daniel Hammer
 */

#include <montauk/syscall.h>
#include <montauk/string.h>
#include <montauk/heap.h>
#include <gui/gui.hpp>
#include <gui/standalone.hpp>
#include <gui/truetype.hpp>

extern "C" {
#include <string.h>
#include <stdio.h>
}

using namespace gui;

// ============================================================================
// Constants
// ============================================================================

static constexpr int WIN_W        = 280;
static constexpr int WIN_H        = 164;
static constexpr int FONT_SIZE    = 16;
static constexpr int FONT_SIZE_LG = 28;

static constexpr int SLIDER_X     = 24;
static constexpr int SLIDER_Y     = 78;
static constexpr int SLIDER_H     = 8;
static constexpr int KNOB_R       = 10;

static constexpr int BTN_W        = 48;
static constexpr int BTN_H        = 28;
static constexpr int BTN_RAD      = 6;

static constexpr Color BG_COLOR     = Color::from_rgb(0xFF, 0xFF, 0xFF);
static constexpr Color TEXT_COLOR   = Color::from_rgb(0x22, 0x22, 0x22);
static constexpr Color ACCENT       = Color::from_rgb(0x36, 0x7B, 0xF0);
static constexpr Color TRACK_BG     = Color::from_rgb(0xDD, 0xDD, 0xDD);
static constexpr Color WHITE        = Color::from_rgb(0xFF, 0xFF, 0xFF);
static constexpr Color MUTE_COLOR   = Color::from_rgb(0xCC, 0x33, 0x33);

// ============================================================================
// State
// ============================================================================

static TrueTypeFont* g_font = nullptr;
static int g_win_w = WIN_W;
static int g_win_h = WIN_H;
static int g_volume = 80;
static bool g_muted = false;
static int g_pre_mute_vol = 80;
static bool g_dragging = false;

// ============================================================================
// Volume helpers
// ============================================================================

static void apply_volume(int vol) {
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    g_volume = vol;
    montauk::audio_set_volume(0, g_volume);
}

static void refresh_volume() {
    int v = montauk::audio_get_volume(0);
    if (v >= 0) g_volume = v;
}

// ============================================================================
// Layout helpers
// ============================================================================

static int slider_w() {
    return g_win_w - 48;
}

static int button_y() {
    return g_win_h - BTN_H - 18;
}

static void get_button_rects(Rect* minus_btn, Rect* plus_btn, Rect* mute_btn) {
    int total_btn_w = BTN_W * 2 + 60 + 12 * 2;
    int bx = (g_win_w - total_btn_w) / 2;
    int by = button_y();
    if (minus_btn) *minus_btn = {bx, by, BTN_W, BTN_H};
    bx += BTN_W + 12;
    if (plus_btn) *plus_btn = {bx, by, BTN_W, BTN_H};
    bx += BTN_W + 12;
    if (mute_btn) *mute_btn = {bx, by, 60, BTN_H};
}

// ============================================================================
// Render
// ============================================================================

static void render(Canvas& canvas) {
    int fh_lg = text_height(g_font, FONT_SIZE_LG);
    int slider_fill_w = slider_w();

    // Background
    canvas.fill(BG_COLOR);

    // Volume percentage (large, centered)
    char vol_str[8];
    snprintf(vol_str, sizeof(vol_str), "%d%%", g_muted ? 0 : g_volume);
    int vw = text_width(g_font, vol_str, FONT_SIZE_LG);
    Color vol_color = g_muted ? MUTE_COLOR : ACCENT;
    draw_text(canvas, g_font, (g_win_w - vw) / 2, 20, vol_str, vol_color, FONT_SIZE_LG);

    // "Muted" label
    if (g_muted) {
        const char* muted_label = "Muted";
        int mw = text_width(g_font, muted_label, FONT_SIZE);
        draw_text(canvas, g_font, (g_win_w - mw) / 2,
                  20 + fh_lg + 4, muted_label, MUTE_COLOR, FONT_SIZE);
    }

    // Slider track
    canvas.fill_rounded_rect(SLIDER_X, SLIDER_Y, slider_fill_w, SLIDER_H, 4, TRACK_BG);

    // Filled portion
    int fill_w = ((g_muted ? 0 : g_volume) * slider_fill_w) / 100;
    if (fill_w > 0)
        canvas.fill_rounded_rect(SLIDER_X, SLIDER_Y, fill_w, SLIDER_H, 4, ACCENT);

    // Knob
    int knob_x = SLIDER_X + fill_w;
    int knob_y = SLIDER_Y + SLIDER_H / 2;
    fill_circle(canvas, knob_x, knob_y, KNOB_R, ACCENT);
    fill_circle(canvas, knob_x, knob_y, KNOB_R - 3, WHITE);

    // Buttons: [-] and [+] and [Mute]
    Rect minus_btn, plus_btn, mute_btn;
    get_button_rects(&minus_btn, &plus_btn, &mute_btn);
    draw_button(canvas, g_font, minus_btn.x, minus_btn.y, minus_btn.w, minus_btn.h,
                "-", Color::from_rgb(0xE0, 0xE0, 0xE0), TEXT_COLOR, BTN_RAD, FONT_SIZE);
    draw_button(canvas, g_font, plus_btn.x, plus_btn.y, plus_btn.w, plus_btn.h,
                "+", Color::from_rgb(0xE0, 0xE0, 0xE0), TEXT_COLOR, BTN_RAD, FONT_SIZE);
    Color mute_bg = g_muted ? MUTE_COLOR : Color::from_rgb(0xE0, 0xE0, 0xE0);
    Color mute_fg = g_muted ? WHITE : TEXT_COLOR;
    draw_button(canvas, g_font, mute_btn.x, mute_btn.y, mute_btn.w, mute_btn.h,
                "Mute", mute_bg, mute_fg, BTN_RAD, FONT_SIZE);
}

// ============================================================================
// Hit testing
// ============================================================================

static int vol_from_slider_x(int mx) {
    int v = ((mx - SLIDER_X) * 100) / slider_w();
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    return v;
}

static bool handle_click(int mx, int my) {
    // Slider area (generous vertical hit zone)
    if (my >= SLIDER_Y - KNOB_R && my <= SLIDER_Y + SLIDER_H + KNOB_R &&
        mx >= SLIDER_X - KNOB_R && mx <= SLIDER_X + slider_w() + KNOB_R) {
        g_muted = false;
        apply_volume(vol_from_slider_x(mx));
        g_dragging = true;
        return true;
    }

    // Buttons
    Rect minus_btn, plus_btn, mute_btn;
    get_button_rects(&minus_btn, &plus_btn, &mute_btn);

    int by = button_y();
    if (my >= by && my < by + BTN_H) {
        // [-] button
        if (minus_btn.contains(mx, my)) {
            g_muted = false;
            apply_volume(g_volume - 5);
            return true;
        }

        // [+] button
        if (plus_btn.contains(mx, my)) {
            g_muted = false;
            apply_volume(g_volume + 5);
            return true;
        }

        // [Mute] button
        if (mute_btn.contains(mx, my)) {
            if (g_muted) {
                g_muted = false;
                apply_volume(g_pre_mute_vol);
            } else {
                g_pre_mute_vol = g_volume;
                g_muted = true;
                montauk::audio_set_volume(0, 0);
            }
            return true;
        }
    }

    return false;
}

// ============================================================================
// Entry point
// ============================================================================

extern "C" void _start() {
    // Load font
    {
        TrueTypeFont* f = (TrueTypeFont*)montauk::malloc(sizeof(TrueTypeFont));
        if (f) {
            montauk::memset(f, 0, sizeof(TrueTypeFont));
            if (!f->init("0:/fonts/Roboto-Medium.ttf")) { montauk::mfree(f); f = nullptr; }
        }
        g_font = f;
    }

    refresh_volume();

    // Create window
    WsWindow win;
    if (!win.create("Volume", WIN_W, WIN_H))
        montauk::exit(1);

    g_win_w = win.width;
    g_win_h = win.height;

    {
        Canvas canvas = win.canvas();
        render(canvas);
    }
    win.present();

    while (win.id >= 0 && !win.closed) {
        Montauk::WinEvent ev;
        int r = win.poll(&ev);

        if (r < 0) break;
        if (r == 0) { montauk::sleep_ms(16); continue; }

        bool redraw = false;

        if (ev.type == 3) break; // close
        if (ev.type == 2) {
            g_win_w = win.width;
            g_win_h = win.height;
            redraw = true;
        }

        // Keyboard
        if (ev.type == 0 && ev.key.pressed) {
            if (ev.key.scancode == 0x01) break; // Escape
            if (ev.key.scancode == 0x4D || ev.key.ascii == '+' || ev.key.ascii == '=') { // Right / +
                g_muted = false;
                apply_volume(g_volume + 5);
                redraw = true;
            } else if (ev.key.scancode == 0x4B || ev.key.ascii == '-') { // Left / -
                g_muted = false;
                apply_volume(g_volume - 5);
                redraw = true;
            } else if (ev.key.ascii == 'm' || ev.key.ascii == 'M') {
                if (g_muted) {
                    g_muted = false;
                    apply_volume(g_pre_mute_vol);
                } else {
                    g_pre_mute_vol = g_volume;
                    g_muted = true;
                    montauk::audio_set_volume(0, 0);
                }
                redraw = true;
            }
        }

        // Mouse
        if (ev.type == 1) {
            bool clicked = (ev.mouse.buttons & 1) && !(ev.mouse.prev_buttons & 1);
            bool released = !(ev.mouse.buttons & 1) && (ev.mouse.prev_buttons & 1);

            if (clicked) {
                if (handle_click(ev.mouse.x, ev.mouse.y))
                    redraw = true;
            }

            if (g_dragging && (ev.mouse.buttons & 1)) {
                g_muted = false;
                apply_volume(vol_from_slider_x(ev.mouse.x));
                redraw = true;
            }

            if (released) {
                g_dragging = false;
            }
        }

        if (redraw) {
            Canvas canvas = win.canvas();
            render(canvas);
            win.present();
        }
    }

    win.destroy();
    montauk::exit(0);
}
