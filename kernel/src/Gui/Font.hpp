/*
    * Font.hpp
    * Embedded bitmap font for GUI rendering
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <cstdint>

namespace Gui {

    constexpr int FONT_WIDTH = 8;
    constexpr int FONT_HEIGHT = 16;

    // Font data - 256 characters, 16 bytes each (8x16 bitmap)
    extern const uint8_t FontData[256][16];

    // Draw a single character
    void DrawChar(int x, int y, char c, uint32_t fg, uint32_t bg);

    void DrawCharTransparent(int x, int y, char c, uint32_t fg);

    void DrawString(int x, int y, const char* str, uint32_t fg, uint32_t bg);

    void DrawStringTransparent(int x, int y, const char* str, uint32_t fg);

    // Measure string width in pixels
    int MeasureString(const char* str);

}
