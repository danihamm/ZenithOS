/*
    * Graphics.cpp
    * Core framebuffer drawing primitives
    * Copyright (c) 2025 Daniel Hammer
*/

#include "Graphics.hpp"

namespace Gui {

    Framebuffer g_fb;

    void InitGraphics(limine_framebuffer* fb) {
        g_fb.address = reinterpret_cast<volatile uint32_t*>(fb->address);
        g_fb.width = fb->width;
        g_fb.height = fb->height;
        g_fb.pitch = fb->pitch;
        g_fb.stride = fb->pitch / 4;
        g_fb.redShift = fb->red_mask_shift;
        g_fb.greenShift = fb->green_mask_shift;
        g_fb.blueShift = fb->blue_mask_shift;
    }

    void PutPixel(int x, int y, uint32_t color) {
        if (x < 0 || y < 0 || x >= (int)g_fb.width || y >= (int)g_fb.height)
            return;
        g_fb.address[x + y * g_fb.stride] = color;
    }

    void DrawHLine(int x, int y, int w, uint32_t color) {
        if (y < 0 || y >= (int)g_fb.height || w <= 0)
            return;

        // Clip to screen bounds
        if (x < 0) {
            w += x;
            x = 0;
        }
        if (x + w > (int)g_fb.width) {
            w = g_fb.width - x;
        }
        if (w <= 0)
            return;

        volatile uint32_t* dest = g_fb.address + x + y * g_fb.stride;
        for (int i = 0; i < w; ++i) {
            dest[i] = color;
        }
    }

    void DrawVLine(int x, int y, int h, uint32_t color) {
        if (x < 0 || x >= (int)g_fb.width || h <= 0)
            return;

        // Clip to screen bounds
        if (y < 0) {
            h += y;
            y = 0;
        }
        if (y + h > (int)g_fb.height) {
            h = g_fb.height - y;
        }
        if (h <= 0)
            return;

        volatile uint32_t* dest = g_fb.address + x + y * g_fb.stride;
        for (int i = 0; i < h; ++i) {
            *dest = color;
            dest += g_fb.stride;
        }
    }

    void FillRect(int x, int y, int w, int h, uint32_t color) {
        if (w <= 0 || h <= 0)
            return;

        // Clip to screen bounds
        if (x < 0) {
            w += x;
            x = 0;
        }
        if (y < 0) {
            h += y;
            y = 0;
        }
        if (x + w > (int)g_fb.width) {
            w = g_fb.width - x;
        }
        if (y + h > (int)g_fb.height) {
            h = g_fb.height - y;
        }
        if (w <= 0 || h <= 0)
            return;

        volatile uint32_t* dest = g_fb.address + x + y * g_fb.stride;
        for (int row = 0; row < h; ++row) {
            for (int col = 0; col < w; ++col) {
                dest[col] = color;
            }
            dest += g_fb.stride;
        }
    }

    void DrawRect(int x, int y, int w, int h, uint32_t color) {
        DrawHLine(x, y, w, color);           // Top
        DrawHLine(x, y + h - 1, w, color);   // Bottom
        DrawVLine(x, y, h, color);           // Left
        DrawVLine(x + w - 1, y, h, color);   // Right
    }

    void ClearScreen(uint32_t color) {
        FillRect(0, 0, g_fb.width, g_fb.height, color);
    }

    void DrawCircle(int cx, int cy, int radius, uint32_t color) {
        int x = 0;
        int y = radius;
        int d = 3 - 2 * radius;
        
        auto drawCirclePoints = [&](int xc, int yc, int x, int y) {
            PutPixel(xc + x, yc + y, color);
            PutPixel(xc - x, yc + y, color);
            PutPixel(xc + x, yc - y, color);
            PutPixel(xc - x, yc - y, color);
            PutPixel(xc + y, yc + x, color);
            PutPixel(xc - y, yc + x, color);
            PutPixel(xc + y, yc - x, color);
            PutPixel(xc - y, yc - x, color);
        };
        
        while (y >= x) {
            drawCirclePoints(cx, cy, x, y);
            x++;
            if (d > 0) {
                y--;
                d = d + 4 * (x - y) + 10;
            } else {
                d = d + 4 * x + 6;
            }
        }
    }

    void FillCircle(int cx, int cy, int radius, uint32_t color) {
        int x = 0;
        int y = radius;
        int d = 3 - 2 * radius;
        
        auto drawFilledCircleLines = [&](int xc, int yc, int x, int y) {
            DrawHLine(xc - x, yc + y, 2 * x + 1, color);
            DrawHLine(xc - x, yc - y, 2 * x + 1, color);
            DrawHLine(xc - y, yc + x, 2 * y + 1, color);
            DrawHLine(xc - y, yc - x, 2 * y + 1, color);
        };
        
        while (y >= x) {
            drawFilledCircleLines(cx, cy, x, y);
            x++;
            if (d > 0) {
                y--;
                d = d + 4 * (x - y) + 10;
            } else {
                d = d + 4 * x + 6;
            }
        }
    }

}
