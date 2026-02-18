/*
    * Cursor.cpp
    * Simple framebuffer mouse cursor with background save/restore
    * Copyright (c) 2025 Daniel Hammer
*/

#include "Cursor.hpp"
#include <Drivers/PS2/Mouse.hpp>
#include <Terminal/Terminal.hpp>

namespace Graphics::Cursor {

    // Classic arrow cursor bitmap (12x16 pixels)
    // Each row is a pair: mask (where cursor has pixels) and fill (white interior)
    // Bit 0 = leftmost pixel in the row
    static constexpr int CursorWidth  = 12;
    static constexpr int CursorHeight = 16;

    // 1 = cursor pixel present
    static constexpr uint16_t CursorMask[CursorHeight] = {
        0b000000000001, // X
        0b000000000011, // XX
        0b000000000111, // XXX
        0b000000001111, // XXXX
        0b000000011111, // XXXXX
        0b000000111111, // XXXXXX
        0b000001111111, // XXXXXXX
        0b000011111111, // XXXXXXXX
        0b000111111111, // XXXXXXXXX
        0b001111111111, // XXXXXXXXXX
        0b011111111111, // XXXXXXXXXXX
        0b111111111111, // XXXXXXXXXXXX
        0b000001111111, // XXXXXXX
        0b000011001111, // XXXX  XX
        0b000110000111, // XXX    XX
        0b000100000011, // XX      X
    };

    // 1 = white fill (interior), 0 = black outline (where mask is set)
    static constexpr uint16_t CursorFill[CursorHeight] = {
        0b000000000000, //
        0b000000000010, //  W
        0b000000000110, //  WW
        0b000000001110, //  WWW
        0b000000011110, //  WWWW
        0b000000111110, //  WWWWW
        0b000001111110, //  WWWWWW
        0b000011111110, //  WWWWWWW
        0b000111111110, //  WWWWWWWW
        0b001111111110, //  WWWWWWWWW
        0b000001111110, //  WWWWWW
        0b000011011110, //  WWWW WW
        0b000000001110, //  WWW
        0b000010000110, //  WW    W
        0b000100000010, //  W      W
        0b000000000000, //
    };

    static constexpr uint32_t ColorBlack = 0x00000000;
    static constexpr uint32_t ColorWhite = 0x00FFFFFF;

    // Framebuffer state
    static uint32_t* g_FbBase   = nullptr;
    static uint64_t  g_FbWidth  = 0;
    static uint64_t  g_FbHeight = 0;
    static uint64_t  g_FbPitch  = 0; // in bytes

    // Saved background under the cursor
    static uint32_t g_SavedBg[CursorWidth * CursorHeight];
    static int32_t  g_OldX = -1;
    static int32_t  g_OldY = -1;

    static inline uint32_t* PixelAt(int32_t x, int32_t y) {
        return reinterpret_cast<uint32_t*>(
            reinterpret_cast<uint8_t*>(g_FbBase) + y * g_FbPitch + x * 4
        );
    }

    static void SaveBackground(int32_t cx, int32_t cy) {
        for (int row = 0; row < CursorHeight; ++row) {
            int32_t py = cy + row;
            if (py < 0 || py >= (int32_t)g_FbHeight) {
                for (int col = 0; col < CursorWidth; ++col)
                    g_SavedBg[row * CursorWidth + col] = 0;
                continue;
            }
            for (int col = 0; col < CursorWidth; ++col) {
                int32_t px = cx + col;
                if (px < 0 || px >= (int32_t)g_FbWidth) {
                    g_SavedBg[row * CursorWidth + col] = 0;
                } else {
                    g_SavedBg[row * CursorWidth + col] = *PixelAt(px, py);
                }
            }
        }
    }

    static void RestoreBackground(int32_t cx, int32_t cy) {
        for (int row = 0; row < CursorHeight; ++row) {
            int32_t py = cy + row;
            if (py < 0 || py >= (int32_t)g_FbHeight) continue;
            for (int col = 0; col < CursorWidth; ++col) {
                int32_t px = cx + col;
                if (px < 0 || px >= (int32_t)g_FbWidth) continue;
                *PixelAt(px, py) = g_SavedBg[row * CursorWidth + col];
            }
        }
    }

    static void DrawCursor(int32_t cx, int32_t cy) {
        for (int row = 0; row < CursorHeight; ++row) {
            int32_t py = cy + row;
            if (py < 0 || py >= (int32_t)g_FbHeight) continue;
            uint16_t mask = CursorMask[row];
            uint16_t fill = CursorFill[row];
            for (int col = 0; col < CursorWidth; ++col) {
                if (!(mask & (1 << col))) continue;
                int32_t px = cx + col;
                if (px < 0 || px >= (int32_t)g_FbWidth) continue;
                uint32_t color = (fill & (1 << col)) ? ColorWhite : ColorBlack;
                *PixelAt(px, py) = color;
            }
        }
    }

    void Initialize(limine_framebuffer* framebuffer) {
        g_FbBase   = reinterpret_cast<uint32_t*>(framebuffer->address);
        g_FbWidth  = framebuffer->width;
        g_FbHeight = framebuffer->height;
        g_FbPitch  = framebuffer->pitch;

        Drivers::PS2::Mouse::SetBounds(
            static_cast<int32_t>(g_FbWidth - 1),
            static_cast<int32_t>(g_FbHeight - 1)
        );

        g_OldX = -1;
        g_OldY = -1;

        Kt::KernelLogStream(Kt::OK, "Graphics/Cursor") << "Cursor initialized ("
            << (uint64_t)g_FbWidth << "x" << (uint64_t)g_FbHeight << ")";
    }

    void Update() {
        auto state = Drivers::PS2::Mouse::GetMouseState();
        int32_t newX = state.X;
        int32_t newY = state.Y;

        // Only redraw if the position changed
        if (newX == g_OldX && newY == g_OldY) return;

        // Restore the old background
        if (g_OldX >= 0 && g_OldY >= 0) {
            RestoreBackground(g_OldX, g_OldY);
        }

        // Save new background, draw cursor
        SaveBackground(newX, newY);
        DrawCursor(newX, newY);

        g_OldX = newX;
        g_OldY = newY;
    }

    uint32_t* GetFramebufferBase()   { return g_FbBase; }
    uint64_t  GetFramebufferWidth()  { return g_FbWidth; }
    uint64_t  GetFramebufferHeight() { return g_FbHeight; }
    uint64_t  GetFramebufferPitch()  { return g_FbPitch; }

};
