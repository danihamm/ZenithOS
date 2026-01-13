/*
    * Icon.hpp
    * Desktop icon with image and label
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <cstdint>

namespace Gui {

    class Icon {
        int m_x;
        int m_y;
        const char* m_label;
        uint32_t m_iconColor;
        bool m_selected;
        
        static constexpr int ICON_SIZE = 48;
        static constexpr int ICON_SPACING = 16;

    public:
        Icon(int x, int y, const char* label, uint32_t iconColor);

        void Render();
        
        // Check if point is inside icon
        bool Contains(int x, int y) const;
        
        // Selection state
        void SetSelected(bool selected) { m_selected = selected; }
        bool IsSelected() const { return m_selected; }
        
        // Accessors
        int GetX() const { return m_x; }
        int GetY() const { return m_y; }
        const char* GetLabel() const { return m_label; }
    };

}
