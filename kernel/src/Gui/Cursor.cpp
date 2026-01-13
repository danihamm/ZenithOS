/*
    * Cursor.cpp
    * Mouse cursor implementation
    * Copyright (c) 2025 Daniel Hammer
*/

#include "Cursor.hpp"
#include "Graphics.hpp"
#include "Color.hpp"

namespace Gui {

    Cursor g_cursor;

    Cursor::Cursor()
        : m_x(0)
        , m_y(0)
        , m_visible(false)
    {
    }

    void Cursor::SetPosition(int x, int y) {
        m_x = x;
        m_y = y;
        
        // Clamp to screen bounds
        if (m_x < 0) m_x = 0;
        if (m_y < 0) m_y = 0;
        if (m_x >= (int)GetScreenWidth()) m_x = GetScreenWidth() - 1;
        if (m_y >= (int)GetScreenHeight()) m_y = GetScreenHeight() - 1;
    }

    void Cursor::Render() {
        if (!m_visible) return;

        // Draw a simple arrow cursor
        // Cursor is 11x16 pixels
        
        // Draw outline (black)
        for (int i = 0; i < 11; i++) {
            PutPixel(m_x, m_y + i, Colors::Background);
        }
        for (int i = 0; i < 8; i++) {
            PutPixel(m_x + i, m_y + i, Colors::Background);
        }
        for (int i = 0; i < 6; i++) {
            PutPixel(m_x + 3 + i, m_y + 11 + i, Colors::Background);
        }
        
        // Fill with white
        for (int row = 1; row < 10; row++) {
            for (int col = 1; col < row && col < 7; col++) {
                PutPixel(m_x + col, m_y + row, Colors::TextBright);
            }
        }
        
        // Bottom part of arrow
        for (int row = 11; row < 16; row++) {
            for (int col = 4; col < 8 && col < row - 6; col++) {
                PutPixel(m_x + col, m_y + row, Colors::TextBright);
            }
        }
    }

}
