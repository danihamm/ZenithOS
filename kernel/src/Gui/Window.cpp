/*
    * Window.cpp
    * Draggable window implementation
    * Copyright (c) 2025 Daniel Hammer
*/

#include "Window.hpp"
#include "Graphics.hpp"
#include "Font.hpp"
#include "Color.hpp"

namespace Gui {

    Window::Window(int x, int y, int w, int h, const char* title, bool closable)
        : Panel(x, y, w, h, title, true)
        , m_dragging(false)
        , m_dragOffsetX(0)
        , m_dragOffsetY(0)
        , m_closeHover(false)
        , m_closable(closable)
    {
    }

    void Window::Render() {
        // Draw window background
        FillRect(m_x, m_y, m_width, m_height, Colors::Surface);
        
        // Draw title bar
        DrawTitleBar();
        
        // Draw window border
        DrawRect(m_x, m_y, m_width, m_height, Colors::Border);
        
        // Draw separator between title bar and content
        DrawHLine(m_x + 1, m_y + TITLE_BAR_HEIGHT, m_width - 2, Colors::Border);
    }

    void Window::DrawTitleBar() {
        // Fill title bar
        FillRect(m_x + 1, m_y + 1, m_width - 2, TITLE_BAR_HEIGHT - 1, Colors::TopBar);
        
        // Draw title text
        if (m_title) {
            DrawStringTransparent(m_x + 8, m_y + 5, m_title, Colors::Text);
        }
        
        // Draw close button
        if (m_closable) {
            DrawCloseButton();
        }
    }

    void Window::DrawCloseButton() {
        int buttonX = m_x + m_width - CLOSE_BUTTON_SIZE - CLOSE_BUTTON_MARGIN - 1;
        int buttonY = m_y + CLOSE_BUTTON_MARGIN;
        
        // Button background
        uint32_t buttonColor = m_closeHover ? Colors::Red : Colors::Overlay;
        FillRect(buttonX, buttonY, CLOSE_BUTTON_SIZE, CLOSE_BUTTON_SIZE, buttonColor);
        
        // Draw X
        uint32_t xColor = m_closeHover ? Colors::TextBright : Colors::Text;
        // Diagonal lines for X
        for (int i = 0; i < CLOSE_BUTTON_SIZE - 6; i++) {
            PutPixel(buttonX + 3 + i, buttonY + 3 + i, xColor);
            PutPixel(buttonX + CLOSE_BUTTON_SIZE - 4 - i, buttonY + 3 + i, xColor);
        }
    }

    bool Window::IsInTitleBar(int x, int y) const {
        return x >= m_x && x < m_x + m_width &&
               y >= m_y && y < m_y + TITLE_BAR_HEIGHT;
    }

    bool Window::IsInCloseButton(int x, int y) const {
        if (!m_closable) return false;

        int buttonX = m_x + m_width - CLOSE_BUTTON_SIZE - CLOSE_BUTTON_MARGIN - 1;
        int buttonY = m_y + CLOSE_BUTTON_MARGIN;
        
        return x >= buttonX && x < buttonX + CLOSE_BUTTON_SIZE &&
               y >= buttonY && y < buttonY + CLOSE_BUTTON_SIZE;
    }

    bool Window::OnMouseDown(int mouseX, int mouseY) {
        if (IsInCloseButton(mouseX, mouseY)) {
            return true; // Close button clicked
        }
        
        if (IsInTitleBar(mouseX, mouseY)) {
            m_dragging = true;
            m_dragOffsetX = mouseX - m_x;
            m_dragOffsetY = mouseY - m_y;
            return false;
        }
        
        return false;
    }

    void Window::OnMouseUp(int mouseX, int mouseY) {
        m_dragging = false;
        
        // Check if mouse is still over close button
        if (IsInCloseButton(mouseX, mouseY)) {
            // Window should be closed (handled by window manager)
        }
    }

    void Window::OnMouseMove(int mouseX, int mouseY) {
        // Update close button hover state
        bool wasHover = m_closeHover;
        m_closeHover = IsInCloseButton(mouseX, mouseY);
        
        if (wasHover != m_closeHover) {
            MarkDirty();
        }
        
        // Handle dragging
        if (m_dragging) {
            m_x = mouseX - m_dragOffsetX;
            m_y = mouseY - m_dragOffsetY;
            
            // Clamp to screen bounds
            if (m_x < 0) m_x = 0;
            if (m_y < 0) m_y = 0;
            if (m_x + m_width > (int)GetScreenWidth()) {
                m_x = GetScreenWidth() - m_width;
            }
            if (m_y + m_height > (int)GetScreenHeight()) {
                m_y = GetScreenHeight() - m_height;
            }
            
            MarkDirty();
        }
    }

}
