/*
 * px.h
 * Shared pixel-level drawing helpers for the MontaukOS Device Explorer
 * Copyright (c) 2026 Daniel Hammer
 */

#pragma once

#include "devexplorer.h"

inline void px_fill(uint32_t* px, int bw, int bh,
                    int x, int y, int w, int h, Color c) {
    uint32_t v = c.to_pixel();
    int x0 = x < 0 ? 0 : x,   y0 = y < 0 ? 0 : y;
    int x1 = x + w > bw ? bw : x + w;
    int y1 = y + h > bh ? bh : y + h;
    for (int row = y0; row < y1; row++)
        for (int col = x0; col < x1; col++)
            px[row * bw + col] = v;
}

inline void px_hline(uint32_t* px, int bw, int bh, int x, int y, int w, Color c) {
    if (y < 0 || y >= bh) return;
    uint32_t v = c.to_pixel();
    int x0 = x < 0 ? 0 : x;
    int x1 = x + w > bw ? bw : x + w;
    for (int col = x0; col < x1; col++)
        px[y * bw + col] = v;
}

inline void px_fill_rounded(uint32_t* px, int bw, int bh,
                             int x, int y, int w, int h, int r, Color c) {
    if (r <= 0) { px_fill(px, bw, bh, x, y, w, h, c); return; }
    uint32_t v = c.to_pixel();
    for (int row = 0; row < h; row++) {
        int dy = y + row;
        if (dy < 0 || dy >= bh) continue;
        for (int col = 0; col < w; col++) {
            int dx = x + col;
            if (dx < 0 || dx >= bw) continue;
            bool in_corner = false;
            int cx_off = 0, cy_off = 0;
            if (col < r && row < r) {
                cx_off = r - col; cy_off = r - row; in_corner = true;
            } else if (col >= w - r && row < r) {
                cx_off = col - (w - r - 1); cy_off = r - row; in_corner = true;
            } else if (col < r && row >= h - r) {
                cx_off = r - col; cy_off = row - (h - r - 1); in_corner = true;
            } else if (col >= w - r && row >= h - r) {
                cx_off = col - (w - r - 1); cy_off = row - (h - r - 1); in_corner = true;
            }
            if (in_corner && cx_off * cx_off + cy_off * cy_off > r * r) continue;
            px[dy * bw + dx] = v;
        }
    }
}

inline void px_text(uint32_t* px, int bw, int bh,
                    int x, int y, const char* text, Color c) {
    if (g_font)
        g_font->draw_to_buffer(px, bw, bh, x, y, text, c, FONT_SIZE);
}

inline int text_w(const char* text) {
    return g_font ? g_font->measure_text(text, FONT_SIZE) : 0;
}

inline int font_h() {
    if (!g_font) return 16;
    auto* cache = g_font->get_cache(FONT_SIZE);
    return cache->ascent - cache->descent;
}

inline void px_button(uint32_t* px, int bw, int bh,
                      int x, int y, int w, int h,
                      const char* label, Color bg, Color fg, int r) {
    px_fill_rounded(px, bw, bh, x, y, w, h, r, bg);
    int tw = text_w(label);
    int fh = font_h();
    px_text(px, bw, bh, x + (w - tw) / 2, y + (h - fh) / 2, label, fg);
}
