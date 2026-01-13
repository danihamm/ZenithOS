/*
    * Icon.cpp
    * Desktop icon implementation
    * Copyright (c) 2025 Daniel Hammer
*/

#include "Icon.hpp"
#include "Graphics.hpp"
#include "Font.hpp"
#include "Color.hpp"

namespace Gui {

    Icon::Icon(int x, int y, const char* label, uint32_t iconColor)
        : m_x(x)
        , m_y(y)
        , m_label(label)
        , m_iconColor(iconColor)
        , m_selected(false)
    {
    }

    void Icon::Render() {
        // Draw icon background (rounded square)
        if (m_selected) {
            FillRect(m_x - 2, m_y - 2, ICON_SIZE + 4, ICON_SIZE + 4, Colors::Blue);
        }
        
        FillRect(m_x, m_y, ICON_SIZE, ICON_SIZE, m_iconColor);
        DrawRect(m_x, m_y, ICON_SIZE, ICON_SIZE, Colors::Border);
        
        // Draw a simple folder/app icon shape inside
        int padding = 8;
        FillRect(m_x + padding, m_y + padding, ICON_SIZE - padding * 2, ICON_SIZE - padding * 2, Colors::Surface);
        
        // Draw label below icon
        if (m_label) {
            int labelWidth = MeasureString(m_label);
            int labelX = m_x + (ICON_SIZE - labelWidth) / 2;
            int labelY = m_y + ICON_SIZE + 4;
            
            // Draw text with shadow for better visibility
            DrawStringTransparent(labelX + 1, labelY + 1, m_label, Colors::Background);
            DrawStringTransparent(labelX, labelY, m_label, Colors::Text);
        }
    }

    bool Icon::Contains(int x, int y) const {
        // Check icon area
        if (x >= m_x && x < m_x + ICON_SIZE &&
            y >= m_y && y < m_y + ICON_SIZE) {
            return true;
        }
        
        // Check label area
        if (m_label) {
            int labelWidth = MeasureString(m_label);
            int labelX = m_x + (ICON_SIZE - labelWidth) / 2;
            int labelY = m_y + ICON_SIZE + 4;
            
            if (x >= labelX && x < labelX + labelWidth &&
                y >= labelY && y < labelY + 16) {
                return true;
            }
        }
        
        return false;
    }

}
