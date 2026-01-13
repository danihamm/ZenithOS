/*
    * Panel.cpp
    * Base class for GUI panels/windows
    * Copyright (c) 2025 Daniel Hammer
*/

#include "Panel.hpp"
#include "Graphics.hpp"
#include "Color.hpp"
#include "Font.hpp"

namespace Gui {

    Panel::Panel(int x, int y, int w, int h, const char* title, bool hasFrame)
        : m_x(x)
        , m_y(y)
        , m_width(w)
        , m_height(h)
        , m_title(title)
        , m_dirty(true)
        , m_hasFrame(hasFrame)
    {
    }

    void Panel::DrawFrame() {
        if (!m_hasFrame) return;

        // Draw border (single pixel, modern flat style)
        DrawRect(m_x, m_y, m_width, m_height, Colors::Border);

        // Draw title if present
        if (m_title != nullptr) {
            int titleX = m_x + 8;
            int titleY = m_y + 4;
            DrawString(titleX, titleY, m_title, Colors::Text, Colors::Surface);
        }
    }

    void Panel::ClearContent(uint32_t color) {
        FillRect(GetContentX(), GetContentY(),
                 GetContentWidth(), GetContentHeight(), color);
    }

}
