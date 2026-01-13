/*
    * WindowManager.hpp
    * Simple panel compositor for fixed-position panels
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include "Panel.hpp"
#include "Window.hpp"
#include "Icon.hpp"

namespace Gui {

    class WindowManager {
        static constexpr int MAX_PANELS = 8;
        static constexpr int MAX_WINDOWS = 8;
        static constexpr int MAX_ICONS = 12;
        
        Panel* m_panels[MAX_PANELS];
        int m_panelCount;
        
        Window* m_windows[MAX_WINDOWS];
        int m_windowCount;
        
        Icon* m_icons[MAX_ICONS];
        int m_iconCount;
        
        Window* m_focusedWindow;
        bool m_initialized;

    public:
        WindowManager();

        // Initialize the window manager (clears screen, sets up state)
        void Init();

        // Add a panel to be managed
        bool AddPanel(Panel* panel);

        // Remove a panel
        void RemovePanel(Panel* panel);
        
        // Window management
        bool AddWindow(Window* window);
        void RemoveWindow(Window* window);
        void BringToFront(Window* window);
        
        // Icon management
        bool AddIcon(Icon* icon);
        void RemoveIcon(Icon* icon);

        // Render all panels
        void RenderAll();

        // Update all panels (check dirty flags, re-render as needed)
        void Update();

        // Force full redraw of all panels
        void Invalidate();
        
        // Mouse event handling
        void OnMouseMove(int x, int y);
        void OnMouseDown(int x, int y);
        void OnMouseUp(int x, int y);

        // Get screen dimensions
        int GetScreenWidth() const;
        int GetScreenHeight() const;

        // Check if initialized
        bool IsInitialized() const { return m_initialized; }
    };

    // Global window manager instance
    extern WindowManager g_wm;

}
