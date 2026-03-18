/*
 * engine.h
 * MontaukOS 2D Game Engine - Core
 * Window management, timing, pixel buffer helpers
 * Copyright (c) 2026 Daniel Hammer
 */

#pragma once
#include <cstdint>
#include <montauk/syscall.h>
#include <montauk/heap.h>
#include <montauk/string.h>
#include <gui/gui.hpp>
#include <gui/truetype.hpp>

extern "C" {
#include <stdio.h>
}

namespace engine {

// ============================================================================
// Core engine
// ============================================================================

struct Engine {
    int win_id = -1;
    uint32_t* pixels = nullptr;
    int screen_w = 0;
    int screen_h = 0;

    // Timing
    uint64_t last_time_ms = 0;
    float dt = 0.016f;       // delta time in seconds
    uint64_t frame_count = 0;

    // Font
    gui::TrueTypeFont* font = nullptr;

    bool running = true;

    // ---- Lifecycle ----

    bool init(const char* title, int w, int h) {
        screen_w = w;
        screen_h = h;

        // Load font
        font = (gui::TrueTypeFont*)montauk::malloc(sizeof(gui::TrueTypeFont));
        if (font) {
            montauk::memset(font, 0, sizeof(gui::TrueTypeFont));
            if (!font->init("0:/fonts/Roboto-Medium.ttf")) {
                montauk::mfree(font);
                font = nullptr;
            }
        }

        // Create window
        Montauk::WinCreateResult wres;
        if (montauk::win_create(title, w, h, &wres) < 0 || wres.id < 0)
            return false;

        win_id = wres.id;
        pixels = (uint32_t*)(uintptr_t)wres.pixelVa;
        last_time_ms = montauk::get_milliseconds();
        return true;
    }

    void update_timing() {
        uint64_t now = montauk::get_milliseconds();
        uint64_t elapsed = now - last_time_ms;
        if (elapsed == 0) elapsed = 1;
        if (elapsed > 100) elapsed = 100; // cap to avoid spiral
        dt = (float)elapsed / 1000.0f;
        last_time_ms = now;
        frame_count++;
    }

    // Poll one window event. Returns true if an event was received.
    bool poll(Montauk::WinEvent* ev) {
        int r = montauk::win_poll(win_id, ev);
        if (r < 0) { running = false; return false; }
        if (r == 0) return false;
        if (ev->type == 3) { running = false; return false; } // close
        if (ev->type == 2) { // resize
            screen_w = ev->resize.w;
            screen_h = ev->resize.h;
            pixels = (uint32_t*)(uintptr_t)montauk::win_resize(
                win_id, screen_w, screen_h);
        }
        return true;
    }

    void present() {
        montauk::win_present(win_id);
    }

    void shutdown() {
        if (win_id >= 0) montauk::win_destroy(win_id);
        win_id = -1;
    }

    // ---- Drawing helpers ----

    void clear(uint32_t color) {
        int count = screen_w * screen_h;
        for (int i = 0; i < count; i++)
            pixels[i] = color;
    }

    void fill_rect(int x, int y, int w, int h, uint32_t color) {
        int x0 = x < 0 ? 0 : x;
        int y0 = y < 0 ? 0 : y;
        int x1 = x + w > screen_w ? screen_w : x + w;
        int y1 = y + h > screen_h ? screen_h : y + h;
        for (int row = y0; row < y1; row++)
            for (int col = x0; col < x1; col++)
                pixels[row * screen_w + col] = color;
    }

    void fill_rect_alpha(int x, int y, int w, int h, uint32_t color) {
        uint8_t sa = (color >> 24) & 0xFF;
        uint8_t sr = (color >> 16) & 0xFF;
        uint8_t sg = (color >> 8) & 0xFF;
        uint8_t sb = color & 0xFF;
        if (sa == 0) return;
        int x0 = x < 0 ? 0 : x;
        int y0 = y < 0 ? 0 : y;
        int x1 = x + w > screen_w ? screen_w : x + w;
        int y1 = y + h > screen_h ? screen_h : y + h;
        for (int row = y0; row < y1; row++) {
            for (int col = x0; col < x1; col++) {
                uint32_t dst = pixels[row * screen_w + col];
                uint8_t dr = (dst >> 16) & 0xFF;
                uint8_t dg = (dst >> 8) & 0xFF;
                uint8_t db = dst & 0xFF;
                uint32_t inv = 255 - sa;
                uint32_t rr = (sa * sr + inv * dr + 128) / 255;
                uint32_t gg = (sa * sg + inv * dg + 128) / 255;
                uint32_t bb = (sa * sb + inv * db + 128) / 255;
                pixels[row * screen_w + col] =
                    0xFF000000 | (rr << 16) | (gg << 8) | bb;
            }
        }
    }

    void draw_rect_outline(int x, int y, int w, int h, uint32_t color) {
        fill_rect(x, y, w, 1, color);
        fill_rect(x, y + h - 1, w, 1, color);
        fill_rect(x, y, 1, h, color);
        fill_rect(x + w - 1, y, 1, h, color);
    }

    void draw_text(int x, int y, const char* text, gui::Color c, int size = 16) {
        if (font)
            font->draw_to_buffer(pixels, screen_w, screen_h, x, y, text, c, size);
    }

    int text_width(const char* text, int size = 16) {
        return font ? font->measure_text(text, size) : 0;
    }

    int text_height(int size = 16) {
        if (!font) return size;
        auto* gc = font->get_cache(size);
        if (!gc) return size;
        return gc->ascent - gc->descent;
    }
};

// ============================================================================
// File loading helper
// ============================================================================

struct FileData {
    uint8_t* data = nullptr;
    uint64_t size = 0;

    bool load(const char* vfs_path) {
        int fd = montauk::open(vfs_path);
        if (fd < 0) return false;
        size = montauk::getsize(fd);
        if (size == 0 || size > 16 * 1024 * 1024) {
            montauk::close(fd);
            return false;
        }
        data = (uint8_t*)montauk::malloc(size);
        if (!data) { montauk::close(fd); return false; }
        montauk::read(fd, data, 0, size);
        montauk::close(fd);
        return true;
    }

    void free() {
        if (data) { montauk::mfree(data); data = nullptr; }
        size = 0;
    }
};

} // namespace engine
