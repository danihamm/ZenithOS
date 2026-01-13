/*
    * DebugGui.cpp
    * Main debug GUI interface
    * Copyright (c) 2025 Daniel Hammer
*/

#include "DebugGui.hpp"
#include "Graphics.hpp"
#include "WindowManager.hpp"
#include "TopBar.hpp"
#include "LogWindow.hpp"
#include "HeapDebugger.hpp"
#include "Color.hpp"
#include <cstddef>

// Placement new for freestanding environment
inline void* operator new(std::size_t, void* ptr) noexcept { return ptr; }

namespace Gui {

    // Static instances for the default panels
    static TopBar* s_topBar = nullptr;
    static LogWindow* s_logWindow = nullptr;
    static HeapDebugger* s_heapDebugger = nullptr;
    static bool s_initialized = false;

    // Since we can't use dynamic memory during early boot
    static char s_topBarStorage[sizeof(TopBar)] __attribute__((aligned(8)));
    static char s_logWindowStorage[sizeof(LogWindow)] __attribute__((aligned(8)));
    static char s_heapDebuggerStorage[sizeof(HeapDebugger)] __attribute__((aligned(8)));

    void Init(limine_framebuffer* fb) {
        InitGraphics(fb);

        g_wm.Init();

        int screenWidth = GetScreenWidth();
        int screenHeight = GetScreenHeight();

        s_topBar = new (s_topBarStorage) TopBar(screenWidth);
        g_wm.AddPanel(s_topBar);

        // Create log window (below top bar, fills remaining space with padding)
        int logX = 8;
        int logY = 24 + 8;  // Below top bar + padding
        int logW = screenWidth - 16;
        int logH = screenHeight - logY - 8;  // Fill to bottom with padding

        s_logWindow = new (s_logWindowStorage) LogWindow(logX, logY, logW, logH);
        g_logWindow = s_logWindow;  // Set global pointer
        g_wm.AddPanel(s_logWindow);
        

        // Initial render
        g_wm.RenderAll();

        s_initialized = true;
    }

    bool IsInitialized() {
        return s_initialized;
    }

    void Log(LogLevel level, const char* component, const char* message) {
        if (!s_initialized || s_logWindow == nullptr) {
            return;
        }

        s_logWindow->AddLine(component, level, message);

        g_wm.Update();
    }

    void UpdateTime(uint16_t year, uint8_t month, uint8_t day,
                    uint8_t hour, uint8_t minute, uint8_t second) {
        if (!s_initialized || s_topBar == nullptr) {
            return;
        }

        s_topBar->SetTime(year, month, day, hour, minute, second);
        g_wm.Update();
    }

    void Refresh() {
        if (!s_initialized) {
            return;
        }

        g_wm.Invalidate();
        g_wm.RenderAll();
    }

    void Update() {
        if (!s_initialized) {
            return;
        }

        g_wm.Update();
    }

}
