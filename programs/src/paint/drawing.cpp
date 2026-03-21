/*
 * drawing.cpp
 * Canvas drawing operations for Paint app
 * Copyright (c) 2026 Daniel Hammer
 */

#include "paint.h"

// ============================================================================
// Basic canvas operations
// ============================================================================

void canvas_put_pixel(int x, int y, Color c) {
    if (x < 0 || x >= g_canvas_w || y < 0 || y >= g_canvas_h) return;
    g_canvas[y * g_canvas_w + x] = c.to_pixel();
}

void canvas_clear(Color c) {
    uint32_t v = c.to_pixel();
    int total = g_canvas_w * g_canvas_h;
    for (int i = 0; i < total; i++)
        g_canvas[i] = v;
}

// ============================================================================
// Brush (filled circle stamp)
// ============================================================================

void canvas_draw_brush(int cx, int cy, Color c, int size) {
    if (size <= 1) {
        canvas_put_pixel(cx, cy, c);
        return;
    }
    int r = size / 2;
    int r2 = r * r;
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r2)
                canvas_put_pixel(cx + dx, cy + dy, c);
}

// ============================================================================
// Bresenham line
// ============================================================================

void canvas_draw_line(int x0, int y0, int x1, int y1, Color c, int thickness) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;

    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    while (true) {
        canvas_draw_brush(x0, y0, c, thickness);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

// ============================================================================
// Rectangle
// ============================================================================

void canvas_draw_rect(int x0, int y0, int x1, int y1, Color c, int thickness) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }

    for (int t = 0; t < thickness; t++) {
        for (int x = x0 + t; x <= x1 - t; x++) {
            canvas_put_pixel(x, y0 + t, c);
            canvas_put_pixel(x, y1 - t, c);
        }
        for (int y = y0 + t; y <= y1 - t; y++) {
            canvas_put_pixel(x0 + t, y, c);
            canvas_put_pixel(x1 - t, y, c);
        }
    }
}

void canvas_fill_rect(int x0, int y0, int x1, int y1, Color c) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }

    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            canvas_put_pixel(x, y, c);
}

// ============================================================================
// Ellipse (midpoint algorithm)
// ============================================================================

static void ellipse_plot4(int cx, int cy, int x, int y, Color c, int thickness) {
    if (thickness <= 1) {
        canvas_put_pixel(cx + x, cy + y, c);
        canvas_put_pixel(cx - x, cy + y, c);
        canvas_put_pixel(cx + x, cy - y, c);
        canvas_put_pixel(cx - x, cy - y, c);
    } else {
        canvas_draw_brush(cx + x, cy + y, c, thickness);
        canvas_draw_brush(cx - x, cy + y, c, thickness);
        canvas_draw_brush(cx + x, cy - y, c, thickness);
        canvas_draw_brush(cx - x, cy - y, c, thickness);
    }
}

void canvas_draw_ellipse(int x0, int y0, int x1, int y1, Color c, int thickness) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }

    int cx = (x0 + x1) / 2;
    int cy = (y0 + y1) / 2;
    int a = (x1 - x0) / 2;
    int b = (y1 - y0) / 2;
    if (a == 0 || b == 0) {
        canvas_draw_line(x0, y0, x1, y1, c, thickness);
        return;
    }

    long long a2 = (long long)a * a;
    long long b2 = (long long)b * b;
    int x = 0, y = b;
    long long d1 = b2 - a2 * b + a2 / 4;

    while (a2 * y > b2 * x) {
        ellipse_plot4(cx, cy, x, y, c, thickness);
        if (d1 < 0) {
            d1 += b2 * (2 * x + 3);
        } else {
            d1 += b2 * (2 * x + 3) + a2 * (-2 * y + 2);
            y--;
        }
        x++;
    }

    long long d2 = b2 * ((long long)(x * 2 + 1) * (x * 2 + 1)) / 4
                 + a2 * ((long long)(y - 1) * (y - 1))
                 - a2 * b2;
    while (y >= 0) {
        ellipse_plot4(cx, cy, x, y, c, thickness);
        if (d2 > 0) {
            d2 += a2 * (-2 * y + 3);
        } else {
            d2 += b2 * (2 * x + 2) + a2 * (-2 * y + 3);
            x++;
        }
        y--;
    }
}

void canvas_fill_ellipse(int x0, int y0, int x1, int y1, Color c) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }

    int cx = (x0 + x1) / 2;
    int cy = (y0 + y1) / 2;
    int a = (x1 - x0) / 2;
    int b = (y1 - y0) / 2;
    if (a == 0 || b == 0) return;

    for (int dy = -b; dy <= b; dy++) {
        // x^2/a^2 + y^2/b^2 <= 1  =>  x <= a * sqrt(1 - y^2/b^2)
        long long x_extent = (long long)a * a * ((long long)b * b - (long long)dy * dy);
        if (x_extent < 0) continue;
        // Integer sqrt approximation
        long long denom = (long long)b * b;
        int w = 0;
        while ((long long)(w + 1) * (w + 1) * denom <= x_extent) w++;
        for (int dx = -w; dx <= w; dx++)
            canvas_put_pixel(cx + dx, cy + dy, c);
    }
}

// ============================================================================
// Flood fill (iterative scanline)
// ============================================================================

void canvas_flood_fill(int sx, int sy, Color fill_color) {
    if (sx < 0 || sx >= g_canvas_w || sy < 0 || sy >= g_canvas_h) return;

    uint32_t target = g_canvas[sy * g_canvas_w + sx];
    uint32_t fill_px = fill_color.to_pixel();
    if (target == fill_px) return;

    // Simple stack-based flood fill with a bounded stack
    static constexpr int STACK_MAX = 65536;
    struct Pt { int16_t x, y; };
    Pt* stack = (Pt*)montauk::malloc(STACK_MAX * sizeof(Pt));
    if (!stack) return;

    int sp = 0;
    stack[sp++] = {(int16_t)sx, (int16_t)sy};
    g_canvas[sy * g_canvas_w + sx] = fill_px;

    while (sp > 0) {
        Pt p = stack[--sp];
        int x = p.x, y = p.y;

        // Scan left
        int left = x;
        while (left > 0 && g_canvas[y * g_canvas_w + left - 1] == target) {
            left--;
            g_canvas[y * g_canvas_w + left] = fill_px;
        }

        // Scan right
        int right = x;
        while (right < g_canvas_w - 1 && g_canvas[y * g_canvas_w + right + 1] == target) {
            right++;
            g_canvas[y * g_canvas_w + right] = fill_px;
        }

        // Check above and below
        for (int dir = -1; dir <= 1; dir += 2) {
            int ny = y + dir;
            if (ny < 0 || ny >= g_canvas_h) continue;
            bool in_span = false;
            for (int px = left; px <= right; px++) {
                if (g_canvas[ny * g_canvas_w + px] == target) {
                    if (!in_span && sp < STACK_MAX) {
                        g_canvas[ny * g_canvas_w + px] = fill_px;
                        stack[sp++] = {(int16_t)px, (int16_t)ny};
                        in_span = true;
                    }
                } else {
                    in_span = false;
                }
            }
        }
    }

    montauk::mfree(stack);
}
