/*
    * WindowManager.cpp
    * Enhanced compositor for panels, windows, and icons
    * Copyright (c) 2025 Daniel Hammer
*/

#include "WindowManager.hpp"
#include "Graphics.hpp"
#include "Color.hpp"
#include "Cursor.hpp"

namespace Gui {

    // Global instance
    WindowManager g_wm;

    WindowManager::WindowManager()
        : m_panelCount(0)
        , m_windowCount(0)
        , m_iconCount(0)
        , m_focusedWindow(nullptr)
        , m_initialized(false)
    {
        for (int i = 0; i < MAX_PANELS; ++i) {
            m_panels[i] = nullptr;
        }
        for (int i = 0; i < MAX_WINDOWS; ++i) {
            m_windows[i] = nullptr;
        }
        for (int i = 0; i < MAX_ICONS; ++i) {
            m_icons[i] = nullptr;
        }
    }

    void WindowManager::Init() {
        // Clear screen with background color
        ClearScreen(Colors::Background);
        m_initialized = true;
    }

    bool WindowManager::AddPanel(Panel* panel) {
        if (m_panelCount >= MAX_PANELS || panel == nullptr) {
            return false;
        }
        m_panels[m_panelCount++] = panel;
        panel->MarkDirty();
        return true;
    }

    void WindowManager::RemovePanel(Panel* panel) {
        for (int i = 0; i < m_panelCount; ++i) {
            if (m_panels[i] == panel) {
                for (int j = i; j < m_panelCount - 1; ++j) {
                    m_panels[j] = m_panels[j + 1];
                }
                m_panels[--m_panelCount] = nullptr;
                return;
            }
        }
    }

    bool WindowManager::AddWindow(Window* window) {
        if (m_windowCount >= MAX_WINDOWS || window == nullptr) {
            return false;
        }
        m_windows[m_windowCount++] = window;
        m_focusedWindow = window;
        return true;
    }

    void WindowManager::RemoveWindow(Window* window) {
        for (int i = 0; i < m_windowCount; ++i) {
            if (m_windows[i] == window) {
                for (int j = i; j < m_windowCount - 1; ++j) {
                    m_windows[j] = m_windows[j + 1];
                }
                m_windows[--m_windowCount] = nullptr;
                if (m_focusedWindow == window) {
                    m_focusedWindow = m_windowCount > 0 ? m_windows[m_windowCount - 1] : nullptr;
                }
                return;
            }
        }
    }

    void WindowManager::BringToFront(Window* window) {
        // Find window
        int index = -1;
        for (int i = 0; i < m_windowCount; ++i) {
            if (m_windows[i] == window) {
                index = i;
                break;
            }
        }
        
        if (index == -1 || index == m_windowCount - 1) {
            return; // Not found or already at front
        }
        
        // Move to end (front)
        for (int i = index; i < m_windowCount - 1; ++i) {
            m_windows[i] = m_windows[i + 1];
        }
        m_windows[m_windowCount - 1] = window;
        m_focusedWindow = window;
    }

    bool WindowManager::AddIcon(Icon* icon) {
        if (m_iconCount >= MAX_ICONS || icon == nullptr) {
            return false;
        }
        m_icons[m_iconCount++] = icon;
        return true;
    }

    void WindowManager::RemoveIcon(Icon* icon) {
        for (int i = 0; i < m_iconCount; ++i) {
            if (m_icons[i] == icon) {
                for (int j = i; j < m_iconCount - 1; ++j) {
                    m_icons[j] = m_icons[j + 1];
                }
                m_icons[--m_iconCount] = nullptr;
                return;
            }
        }
    }

    void WindowManager::RenderAll() {
        // Clear background
        ClearScreen(Colors::Background);
        
        // Render icons first (on desktop)
        for (int i = 0; i < m_iconCount; ++i) {
            if (m_icons[i] != nullptr) {
                m_icons[i]->Render();
            }
        }
        
        // Render fixed panels (like top bar, log window)
        for (int i = 0; i < m_panelCount; ++i) {
            if (m_panels[i] != nullptr) {
                m_panels[i]->Render();
                m_panels[i]->ClearDirty();
            }
        }
        
        // Render windows in z-order (back to front)
        for (int i = 0; i < m_windowCount; ++i) {
            if (m_windows[i] != nullptr) {
                m_windows[i]->Render();
            }
        }
        
        // Render cursor last (on top of everything)
        g_cursor.Render();
    }

    void WindowManager::Update() {
        bool needsRedraw = false;
        
        // Update all panels (this allows them to refresh their data)
        for (int i = 0; i < m_panelCount; ++i) {
            if (m_panels[i] != nullptr) {
                m_panels[i]->Update();
                if (m_panels[i]->IsDirty()) {
                    needsRedraw = true;
                }
            }
        }
        
        // Check windows
        for (int i = 0; i < m_windowCount; ++i) {
            if (m_windows[i] != nullptr && m_windows[i]->IsDirty()) {
                needsRedraw = true;
                break;
            }
        }
        
        if (needsRedraw) {
            RenderAll();
        } else {
            // Just redraw cursor if nothing else changed
            g_cursor.Render();
        }
    }

    void WindowManager::Invalidate() {
        for (int i = 0; i < m_panelCount; ++i) {
            if (m_panels[i] != nullptr) {
                m_panels[i]->MarkDirty();
            }
        }
        for (int i = 0; i < m_windowCount; ++i) {
            if (m_windows[i] != nullptr) {
                m_windows[i]->MarkDirty();
            }
        }
    }

    void WindowManager::OnMouseMove(int x, int y) {
        g_cursor.SetPosition(x, y);
        
        // Update focused window if dragging
        if (m_focusedWindow && m_focusedWindow->IsDragging()) {
            m_focusedWindow->OnMouseMove(x, y);
        } else if (m_focusedWindow) {
            // Update hover states
            m_focusedWindow->OnMouseMove(x, y);
        }
    }

    void WindowManager::OnMouseDown(int x, int y) {
        // Check windows from front to back
        for (int i = m_windowCount - 1; i >= 0; --i) {
            Window* win = m_windows[i];
            if (win && x >= win->GetX() && x < win->GetX() + win->GetWidth() &&
                y >= win->GetY() && y < win->GetY() + win->GetHeight()) {
                
                BringToFront(win);
                bool shouldClose = win->OnMouseDown(x, y);
                if (shouldClose) {
                    RemoveWindow(win);
                    Invalidate();
                }
                return;
            }
        }
        
        // Check icons
        for (int i = 0; i < m_iconCount; ++i) {
            if (m_icons[i] && m_icons[i]->Contains(x, y)) {
                // Deselect all others
                for (int j = 0; j < m_iconCount; ++j) {
                    if (m_icons[j]) m_icons[j]->SetSelected(false);
                }
                m_icons[i]->SetSelected(true);
                Invalidate();
                return;
            }
        }
        
        // Clicked on desktop - deselect all icons
        for (int i = 0; i < m_iconCount; ++i) {
            if (m_icons[i]) m_icons[i]->SetSelected(false);
        }
        Invalidate();
    }

    void WindowManager::OnMouseUp(int x, int y) {
        if (m_focusedWindow) {
            m_focusedWindow->OnMouseUp(x, y);
        }
    }

    int WindowManager::GetScreenWidth() const {
        return Gui::GetScreenWidth();
    }

    int WindowManager::GetScreenHeight() const {
        return Gui::GetScreenHeight();
    }

}

