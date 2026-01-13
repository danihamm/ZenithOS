/*
    * Panel.hpp
    * Base class for GUI panels/windows
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <cstdint>

namespace Gui {

    class Panel {
    protected:
        int m_x;
        int m_y;
        int m_width;
        int m_height;
        const char* m_title;
        bool m_dirty;
        bool m_hasFrame;

    public:
        Panel(int x, int y, int w, int h, const char* title = nullptr, bool hasFrame = true);
        virtual ~Panel() = default;

        // Pure virtual - each panel must implement rendering
        virtual void Render() = 0;

        // Optional update hook (called before render if dirty)
        virtual void Update() {}

        // Mark panel as needing redraw
        void MarkDirty() { m_dirty = true; }
        bool IsDirty() const { return m_dirty; }
        void ClearDirty() { m_dirty = false; }

        // Draw the panel frame (modern flat style)
        void DrawFrame();

        // Clear the panel content area
        void ClearContent(uint32_t color);

        // Accessors
        int GetX() const { return m_x; }
        int GetY() const { return m_y; }
        int GetWidth() const { return m_width; }
        int GetHeight() const { return m_height; }

        // Content area (inside frame) - virtual so Window can override
        virtual int GetContentY() const { return m_hasFrame ? m_y + 1 : m_y; }
        virtual int GetContentHeight() const { return m_hasFrame ? m_height - 2 : m_height; }
        
        int GetContentX() const { return m_hasFrame ? m_x + 1 : m_x; }
        int GetContentWidth() const { return m_hasFrame ? m_width - 2 : m_width; }
    };

}
