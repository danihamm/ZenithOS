/*
    * standalone.hpp
    * MontaukOS helpers for standalone Window Server apps
    * Copyright (c) 2026 Daniel Hammer
*/

#pragma once
#include <montauk/syscall.h>
#include "gui/canvas.hpp"
#include "gui/svg.hpp"
#include "gui/truetype.hpp"

namespace gui {

struct WsWindow {
    int id;
    uint32_t* pixels;
    int width;
    int height;
    int scale_factor;
    bool closed;

    WsWindow()
        : id(-1), pixels(nullptr), width(0), height(0), scale_factor(1), closed(false) {}

    bool create(const char* title, int w, int h) {
        Montauk::WinCreateResult wres;
        if (montauk::win_create(title, w, h, &wres) < 0 || wres.id < 0)
            return false;

        id = wres.id;
        pixels = (uint32_t*)(uintptr_t)wres.pixelVa;
        width = w;
        height = h;
        scale_factor = montauk::win_getscale();
        closed = false;
        return true;
    }

    int poll(Montauk::WinEvent* ev) {
        if (id < 0 || closed) return -1;

        int r = montauk::win_poll(id, ev);
        if (r <= 0) return r;

        if (ev->type == 2) {
            width = ev->resize.w;
            height = ev->resize.h;
            pixels = (uint32_t*)(uintptr_t)montauk::win_resize(id, width, height);
        } else if (ev->type == 3) {
            closed = true;
        } else if (ev->type == 4) {
            scale_factor = ev->scale.scale;
        }

        return r;
    }

    Canvas canvas() const {
        return Canvas(pixels, width, height);
    }

    void present() const {
        if (id >= 0)
            montauk::win_present(id);
    }

    void set_cursor(int cursor) const {
        if (id >= 0)
            montauk::win_setcursor(id, cursor);
    }

    void destroy() {
        if (id >= 0)
            montauk::win_destroy(id);
        id = -1;
        pixels = nullptr;
        width = 0;
        height = 0;
        closed = true;
    }
};

inline int text_width(TrueTypeFont* font, const char* text, int size) {
    if (font && font->valid)
        return font->measure_text(text, size);
    int len = 0;
    while (text && text[len]) len++;
    return len * FONT_WIDTH;
}

inline int text_height(TrueTypeFont* font, int size) {
    if (font && font->valid) {
        GlyphCache* cache = font->get_cache(size);
        if (cache)
            return cache->ascent - cache->descent;
    }
    return size;
}

inline void draw_text(Canvas& c, TrueTypeFont* font, int x, int y,
                      const char* text, Color color, int size) {
    if (!text || !text[0]) return;
    if (font && font->valid)
        font->draw_to_buffer(c.pixels, c.w, c.h, x, y, text, color, size);
}

inline void fill_circle(Canvas& c, int cx, int cy, int r, Color color) {
    if (r <= 0) return;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r)
                c.put_pixel(cx + dx, cy + dy, color);
        }
    }
}

inline void draw_button(Canvas& c, TrueTypeFont* font, int x, int y, int w, int h,
                        const char* label, Color bg, Color fg, int radius,
                        int size = 16) {
    c.fill_rounded_rect(x, y, w, h, radius, bg);
    int tw = text_width(font, label, size);
    int th = text_height(font, size);
    draw_text(c, font, x + (w - tw) / 2, y + (h - th) / 2, label, fg, size);
}

inline void draw_icon(Canvas& c, int x, int y, const SvgIcon& icon) {
    c.icon(x, y, icon);
}

} // namespace gui
