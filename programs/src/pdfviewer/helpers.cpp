/*
 * helpers.cpp
 * Pixel drawing and string helpers
 * Copyright (c) 2026 Daniel Hammer
 */

#include "pdfviewer.h"

void px_fill(uint32_t* px, int bw, int bh,
             int x, int y, int w, int h, Color c) {
    uint32_t v = c.to_pixel();
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > bw ? bw : x + w;
    int y1 = y + h > bh ? bh : y + h;
    for (int row = y0; row < y1; row++)
        for (int col = x0; col < x1; col++)
            px[row * bw + col] = v;
}

void px_hline(uint32_t* px, int bw, int bh,
              int x, int y, int w, Color c) {
    if (y < 0 || y >= bh) return;
    uint32_t v = c.to_pixel();
    int x0 = x < 0 ? 0 : x;
    int x1 = x + w > bw ? bw : x + w;
    for (int col = x0; col < x1; col++)
        px[y * bw + col] = v;
}

void px_vline(uint32_t* px, int bw, int bh,
              int x, int y, int h, Color c) {
    if (x < 0 || x >= bw) return;
    uint32_t v = c.to_pixel();
    int y0 = y < 0 ? 0 : y;
    int y1 = y + h > bh ? bh : y + h;
    for (int row = y0; row < y1; row++)
        px[row * bw + x] = v;
}

void px_rect(uint32_t* px, int bw, int bh,
             int x, int y, int w, int h, Color c) {
    px_hline(px, bw, bh, x, y, w, c);
    px_hline(px, bw, bh, x, y + h - 1, w, c);
    px_vline(px, bw, bh, x, y, h, c);
    px_vline(px, bw, bh, x + w - 1, y, h, c);
}

void px_fill_rounded(uint32_t* px, int bw, int bh,
                     int x, int y, int w, int h, int r, Color c) {
    uint32_t v = c.to_pixel();
    for (int row = 0; row < h; row++) {
        int py = y + row;
        if (py < 0 || py >= bh) continue;
        int inset = 0;
        if (row < r) {
            int dy = r - 1 - row;
            if (r == 3) {
                if (dy >= 2) inset = 2;
                else if (dy >= 1) inset = 1;
            } else {
                for (int i = r; i > 0; i--) {
                    int dx = r - i;
                    if (dx * dx + dy * dy < r * r) { inset = i; break; }
                }
            }
        } else if (row >= h - r) {
            int dy = row - (h - r);
            if (r == 3) {
                if (dy >= 2) inset = 2;
                else if (dy >= 1) inset = 1;
            } else {
                for (int i = r; i > 0; i--) {
                    int dx = r - i;
                    if (dx * dx + dy * dy < r * r) { inset = i; break; }
                }
            }
        }
        int x0 = x + inset;
        int x1 = x + w - inset;
        if (x0 < 0) x0 = 0;
        if (x1 > bw) x1 = bw;
        for (int col = x0; col < x1; col++)
            px[py * bw + col] = v;
    }
}

void px_line(uint32_t* px, int bw, int bh,
             int x0, int y0, int x1, int y1, int thick, Color c) {
    if (thick < 1) thick = 1;
    int half = thick / 2;

    // Horizontal line
    if (y0 == y1) {
        int lx = x0 < x1 ? x0 : x1;
        int rx = x0 > x1 ? x0 : x1;
        px_fill(px, bw, bh, lx, y0 - half, rx - lx + 1, thick, c);
        return;
    }
    // Vertical line
    if (x0 == x1) {
        int ty = y0 < y1 ? y0 : y1;
        int by = y0 > y1 ? y0 : y1;
        px_fill(px, bw, bh, x0 - half, ty, thick, by - ty + 1, c);
        return;
    }
    // General case: Bresenham with thickness
    uint32_t v = c.to_pixel();
    int dx = x1 - x0; int sx = dx > 0 ? 1 : -1; if (dx < 0) dx = -dx;
    int dy = y1 - y0; int sy = dy > 0 ? 1 : -1; if (dy < 0) dy = -dy;
    int err = dx - dy;
    int cx = x0, cy = y0;
    while (true) {
        for (int oy = -half; oy <= half; oy++) {
            for (int ox = -half; ox <= half; ox++) {
                int px_x = cx + ox, py_y = cy + oy;
                if (px_x >= 0 && px_x < bw && py_y >= 0 && py_y < bh)
                    px[py_y * bw + px_x] = v;
            }
        }
        if (cx == x1 && cy == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; cx += sx; }
        if (e2 < dx) { err += dx; cy += sy; }
    }
}

int str_len(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

void str_cpy(char* dst, const char* src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}
