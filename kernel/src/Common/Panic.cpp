/*
    * Panic.cpp
    * Copyright (c) 2025 Daniel Hammer
*/

#include "Panic.hpp"
#include "../CppLib/BoxUI.hpp"

#include <Gui/DebugGui.hpp>
#include <Gui/WindowManager.hpp>
#include <Gui/PanicWindow.hpp>

// Static storage for panic window to avoid heap allocation during crash
static char s_panicWindowStorage[sizeof(Gui::PanicWindow)];
static Gui::PanicWindow* s_panicWindow = nullptr;

// Placement new for freestanding environment
inline void* operator new(std::size_t, void* ptr) noexcept { return ptr; }

void Panic(const char *meditationString, System::PanicFrame* frame) {
    // Disable interrupts immediately
    asm volatile ("cli");

    // Ensure GUI is initialized if possible. If the panic happens *during* Gui::Init, this might be tricky,
    // but assuming we are past basic boot.
    if (Gui::IsInitialized()) {
        // Create the window using placement new
        s_panicWindow = new (s_panicWindowStorage) Gui::PanicWindow(meditationString, frame);
        
        // Add to WindowManager
        Gui::g_wm.AddPanel(s_panicWindow);
        
        // Bring to front
        Gui::g_wm.BringToFront(s_panicWindow);
        
        Gui::Refresh();

        while (true) {    
            asm ("hlt");
        }
    } else {
        // Fallback to ANSI text panic if GUI isn't ready
        const int boxWidth = 72;
        kerr << BOXUI_ANSI_RED_BG << BOXUI_ANSI_WHITE_FG << BOXUI_ANSI_BOLD << "\n"; // ... existing fallback code
        PrintBoxedLine(kerr, "!!! KERNEL PANIC (NO GUI) !!!", boxWidth, true);
        PrintBoxedLine(kerr, meditationString, boxWidth);
        // ... simplistic fallback
        while(true) asm("hlt");
    }
}