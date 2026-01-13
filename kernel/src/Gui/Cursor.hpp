/*
    * Cursor.hpp
    * Mouse cursor rendering
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <cstdint>

namespace Gui {

    class Cursor {
        int m_x;
        int m_y;
        bool m_visible;

    public:
        Cursor();

        void SetPosition(int x, int y);
        void Show() { m_visible = true; }
        void Hide() { m_visible = false; }
        
        void Render();
        
        int GetX() const { return m_x; }
        int GetY() const { return m_y; }
        bool IsVisible() const { return m_visible; }
    };

    // Global cursor instance
    extern Cursor g_cursor;

}
