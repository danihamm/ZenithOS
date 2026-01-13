/*
    * Window.hpp
    * Draggable window class for desktop environment
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include "Panel.hpp"
#include <cstdint>

namespace Gui {

    class Window : public Panel {
        static constexpr int TITLE_BAR_HEIGHT = 24;
        static constexpr int CLOSE_BUTTON_SIZE = 16;
        static constexpr int CLOSE_BUTTON_MARGIN = 4;

        bool m_dragging;
        int m_dragOffsetX;
        int m_dragOffsetY;
        bool m_closeHover;
        bool m_closable;

    public:
        Window(int x, int y, int w, int h, const char* title, bool closable = true);

        void Render() override;

        // Mouse event handlers
        bool OnMouseDown(int mouseX, int mouseY);
        void OnMouseUp(int mouseX, int mouseY);
        void OnMouseMove(int mouseX, int mouseY);

        // Check if point is in title bar
        bool IsInTitleBar(int x, int y) const;
        
        // Check if point is in close button
        bool IsInCloseButton(int x, int y) const;

        // Get content area (below title bar)
        int GetContentY() const override { return m_y + TITLE_BAR_HEIGHT + 1; }
        int GetContentHeight() const override { return m_height - TITLE_BAR_HEIGHT - 2; }

        // Check if currently dragging
        bool IsDragging() const { return m_dragging; }

    protected:
        // Draw the title bar
        void DrawTitleBar();
        
        // Draw the close button
        void DrawCloseButton();
    };

}
