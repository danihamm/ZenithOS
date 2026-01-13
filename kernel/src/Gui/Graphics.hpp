/*
    * Graphics.hpp
    * Core framebuffer drawing primitives
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <cstdint>
#include <cstddef>
#include <limine.h>

namespace Gui {

    struct Framebuffer {
        volatile uint32_t* address;
        uint32_t width;
        uint32_t height;
        uint32_t pitch;      // Bytes per row
        uint32_t stride;     // Pixels per row (pitch / 4)
        uint8_t redShift;
        uint8_t greenShift;
        uint8_t blueShift;
    };

    extern Framebuffer g_fb;

    // Initialize graphics subsystem with Limine framebuffer
    void InitGraphics(limine_framebuffer* fb);

    // Get framebuffer dimensions
    inline uint32_t GetScreenWidth() { return g_fb.width; }
    inline uint32_t GetScreenHeight() { return g_fb.height; }

    // Create a color from RGB components
    inline uint32_t MakeColor(uint8_t r, uint8_t g, uint8_t b) {
        return (r << g_fb.redShift) | (g << g_fb.greenShift) | (b << g_fb.blueShift);
    }

    // Basic drawing primitives
    void PutPixel(int x, int y, uint32_t color);
    void FillRect(int x, int y, int w, int h, uint32_t color);
    void DrawRect(int x, int y, int w, int h, uint32_t color);
    void DrawHLine(int x, int y, int w, uint32_t color);
    void DrawVLine(int x, int y, int h, uint32_t color);

    // Advanced shapes
    void DrawCircle(int cx, int cy, int radius, uint32_t color);
    void FillCircle(int cx, int cy, int radius, uint32_t color);

    // Clear entire screen
    void ClearScreen(uint32_t color);

}
