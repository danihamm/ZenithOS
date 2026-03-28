/*
 * main.cpp
 * MontaukOS Mandelbrot - standalone Window Server app
 * Preserves the desktop app's fixed-point renderer and interaction model
 * Copyright (c) 2026 Daniel Hammer
 */

#include <montauk/syscall.h>
#include <montauk/string.h>
#include <montauk/heap.h>
#include <gui/gui.hpp>
#include <gui/standalone.hpp>
#include <gui/truetype.hpp>
#include <gui/widgets.hpp>

extern "C" {
#include <stdio.h>
}

using namespace gui;

// Match the old desktop window's outer 500x400 frame once the desktop adds
// its usual titlebar and borders around this external app.
static constexpr int INIT_W = 498;
static constexpr int INIT_H = 369;

static constexpr int MB_MAX_ITER = 256;
static constexpr int MB_TOOLBAR_H = 32;
static constexpr int UI_FONT_SIZE = 18;

using fp_t = int64_t;
static constexpr int FP_SHIFT = 36;

static TrueTypeFont* g_font = nullptr;

static inline fp_t fp_from_int(int v) { return (fp_t)v << FP_SHIFT; }

static inline fp_t fp_mul(fp_t a, fp_t b) {
    __int128 r = (__int128)a * b;
    return (fp_t)(r >> FP_SHIFT);
}

static inline fp_t fp_div_small(int a, int b) {
    return ((fp_t)a << FP_SHIFT) / (fp_t)b;
}

struct MandelbrotState {
    fp_t center_x;
    fp_t center_y;
    fp_t scale;
    int max_iter;
    bool needs_render;

    bool dragging;
    int drag_start_x;
    int drag_start_y;
    fp_t drag_start_cx;
    fp_t drag_start_cy;
};

static int ui_text_h() {
    return text_height(g_font, UI_FONT_SIZE);
}

static int ui_text_w(const char* text) {
    return text_width(g_font, text, UI_FONT_SIZE);
}

static void ui_draw_text(Canvas& c, int x, int y, const char* text, Color color) {
    if (g_font && g_font->valid)
        draw_text(c, g_font, x, y, text, color, UI_FONT_SIZE);
}

static void reset_view(MandelbrotState* mb, int width_hint) {
    mb->center_x = -fp_from_int(1) / 2;
    mb->center_y = 0;
    mb->scale = fp_div_small(3, width_hint > 0 ? width_hint : 400);
    mb->max_iter = MB_MAX_ITER;
    mb->needs_render = true;
    mb->dragging = false;
}

static uint32_t mandelbrot_color(int iter, int max_iter) {
    if (iter >= max_iter) return 0xFF000000;

    int t = (iter * 7) & 0xFF;
    int phase = (iter * 7) >> 8;

    uint8_t r, g, b;
    switch (phase % 6) {
    case 0: r = 255; g = (uint8_t)t; b = 0; break;
    case 1: r = (uint8_t)(255 - t); g = 255; b = 0; break;
    case 2: r = 0; g = 255; b = (uint8_t)t; break;
    case 3: r = 0; g = (uint8_t)(255 - t); b = 255; break;
    case 4: r = (uint8_t)t; g = 0; b = 255; break;
    default: r = 255; g = 0; b = (uint8_t)(255 - t); break;
    }

    return 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static void mandelbrot_render(MandelbrotState* mb, uint32_t* pixels, int w, int h) {
    int render_h = h - MB_TOOLBAR_H;
    if (render_h <= 0) return;

    fp_t half_w = fp_mul(fp_from_int(w / 2), mb->scale);
    fp_t half_h = fp_mul(fp_from_int(render_h / 2), mb->scale);
    fp_t x_min = mb->center_x - half_w;
    fp_t y_min = mb->center_y - half_h;
    fp_t bailout = fp_from_int(4);

    for (int py = 0; py < render_h; py++) {
        fp_t ci = y_min + fp_mul(fp_from_int(py), mb->scale);
        uint32_t* row = pixels + (py + MB_TOOLBAR_H) * w;

        for (int px = 0; px < w; px++) {
            fp_t cr = x_min + fp_mul(fp_from_int(px), mb->scale);

            fp_t zr = 0;
            fp_t zi = 0;
            int iter = 0;

            while (iter < mb->max_iter) {
                fp_t zr2 = fp_mul(zr, zr);
                fp_t zi2 = fp_mul(zi, zi);
                if (zr2 + zi2 > bailout) break;
                fp_t new_zr = zr2 - zi2 + cr;
                zi = fp_mul(fp_from_int(2), fp_mul(zr, zi)) + ci;
                zr = new_zr;
                iter++;
            }

            row[px] = mandelbrot_color(iter, mb->max_iter);
        }
    }

    mb->needs_render = false;
}

static void draw_toolbar(MandelbrotState* mb, Canvas& c) {
    c.fill_rect(0, 0, c.w, MB_TOOLBAR_H, Color::from_rgb(0xF5, 0xF5, 0xF5));
    c.hline(0, MB_TOOLBAR_H - 1, c.w, colors::BORDER);

    draw_button(c, g_font, 8, 4, 60, 24, "Reset",
                colors::ACCENT, colors::WHITE, 4, UI_FONT_SIZE);

    char iter_str[24];
    snprintf(iter_str, sizeof(iter_str), "Iter: %d", mb->max_iter);

    int text_y = (MB_TOOLBAR_H - ui_text_h()) / 2;
    ui_draw_text(c, 80, text_y, iter_str, colors::TEXT_COLOR);

    int iter_text_w = ui_text_w(iter_str);
    int btn_x = 80 + iter_text_w + 8;
    Color step_bg = Color::from_rgb(0xAA, 0xAA, 0xAA);
    draw_button(c, g_font, btn_x, 4, 24, 24, "-",
                step_bg, colors::WHITE, 4, UI_FONT_SIZE);
    draw_button(c, g_font, btn_x + 28, 4, 24, 24, "+",
                step_bg, colors::WHITE, 4, UI_FONT_SIZE);

    const char* hint = "Scroll=zoom  Drag=pan";
    int hint_w = ui_text_w(hint);
    ui_draw_text(c, c.w - hint_w - 8, text_y, hint, Color::from_rgb(0x99, 0x99, 0x99));
}

static void render(MandelbrotState* mb, WsWindow& win) {
    Canvas c = win.canvas();

    if (mb->needs_render)
        mandelbrot_render(mb, c.pixels, c.w, c.h);

    draw_toolbar(mb, c);
}

static bool handle_mouse(MandelbrotState* mb, const MouseEvent& ev, int win_w, int win_h) {
    int lx = ev.x;
    int ly = ev.y;

    if (ev.left_pressed() && ly < MB_TOOLBAR_H) {
        Rect reset_r = {8, 4, 60, 24};
        if (reset_r.contains(lx, ly)) {
            reset_view(mb, win_w);
            return true;
        }

        char iter_str[24];
        snprintf(iter_str, sizeof(iter_str), "Iter: %d", mb->max_iter);
        int iter_text_w = ui_text_w(iter_str);
        int btn_x = 80 + iter_text_w + 8;

        Rect minus_r = {btn_x, 4, 24, 24};
        Rect plus_r = {btn_x + 28, 4, 24, 24};

        if (minus_r.contains(lx, ly)) {
            if (mb->max_iter > 32) {
                mb->max_iter /= 2;
                mb->needs_render = true;
                return true;
            }
            return false;
        }

        if (plus_r.contains(lx, ly)) {
            if (mb->max_iter < 4096) {
                mb->max_iter *= 2;
                mb->needs_render = true;
                return true;
            }
            return false;
        }

        return false;
    }

    if (ev.left_pressed() && ly >= MB_TOOLBAR_H) {
        mb->dragging = true;
        mb->drag_start_x = lx;
        mb->drag_start_y = ly;
        mb->drag_start_cx = mb->center_x;
        mb->drag_start_cy = mb->center_y;
    }

    if (mb->dragging && ev.left_held()) {
        int dx = lx - mb->drag_start_x;
        int dy = ly - mb->drag_start_y;
        mb->center_x = mb->drag_start_cx - fp_mul(fp_from_int(dx), mb->scale);
        mb->center_y = mb->drag_start_cy - fp_mul(fp_from_int(dy), mb->scale);
        mb->needs_render = true;
        return true;
    }

    if (mb->dragging && !ev.left_held())
        mb->dragging = false;

    if (ev.scroll != 0 && ly >= MB_TOOLBAR_H) {
        int render_h = win_h - MB_TOOLBAR_H;
        fp_t mx_frac = mb->center_x + fp_mul(fp_from_int(lx - win_w / 2), mb->scale);
        fp_t my_frac = mb->center_y + fp_mul(fp_from_int((ly - MB_TOOLBAR_H) - render_h / 2), mb->scale);

        if (ev.scroll < 0)
            mb->scale = fp_mul(mb->scale, fp_from_int(3) / 4);
        else
            mb->scale = fp_mul(mb->scale, fp_from_int(4) / 3);

        fp_t new_mx = mb->center_x + fp_mul(fp_from_int(lx - win_w / 2), mb->scale);
        fp_t new_my = mb->center_y + fp_mul(fp_from_int((ly - MB_TOOLBAR_H) - render_h / 2), mb->scale);
        mb->center_x += mx_frac - new_mx;
        mb->center_y += my_frac - new_my;
        mb->needs_render = true;
        return true;
    }

    return false;
}

static bool handle_key(MandelbrotState* mb, const Montauk::KeyEvent& key, int win_w) {
    if (!key.pressed) return false;

    if (key.ascii == 'r' || key.ascii == 'R') {
        reset_view(mb, win_w);
        return true;
    }

    if (key.ascii == '+' || key.ascii == '=') {
        if (mb->max_iter < 4096) {
            mb->max_iter *= 2;
            mb->needs_render = true;
            return true;
        }
        return false;
    }

    if (key.ascii == '-') {
        if (mb->max_iter > 32) {
            mb->max_iter /= 2;
            mb->needs_render = true;
            return true;
        }
        return false;
    }

    return false;
}

extern "C" void _start() {
    g_font = (TrueTypeFont*)montauk::malloc(sizeof(TrueTypeFont));
    if (g_font) {
        montauk::memset(g_font, 0, sizeof(TrueTypeFont));
        if (!g_font->init("0:/fonts/Roboto-Medium.ttf")) {
            montauk::mfree(g_font);
            g_font = nullptr;
        }
    }

    MandelbrotState mb = {};
    reset_view(&mb, INIT_W);

    WsWindow win;
    if (!win.create("Mandelbrot", INIT_W, INIT_H))
        montauk::exit(1);

    render(&mb, win);
    win.present();

    while (true) {
        Montauk::WinEvent ev;
        int r = win.poll(&ev);

        if (r < 0) break;

        if (r == 0) {
            montauk::sleep_ms(16);
            continue;
        }

        if (ev.type == 3) break;

        bool redraw = false;

        if (ev.type == 2) {
            mb.needs_render = true;
            redraw = true;
        } else if (ev.type == 1) {
            MouseEvent mev = {
                ev.mouse.x,
                ev.mouse.y,
                ev.mouse.buttons,
                ev.mouse.prev_buttons,
                ev.mouse.scroll
            };
            redraw = handle_mouse(&mb, mev, win.width, win.height);
        } else if (ev.type == 0) {
            redraw = handle_key(&mb, ev.key, win.width);
        } else if (ev.type == 4) {
            redraw = true;
        }

        if (redraw || mb.needs_render) {
            render(&mb, win);
            win.present();
        }
    }

    win.destroy();
    montauk::exit(0);
}
