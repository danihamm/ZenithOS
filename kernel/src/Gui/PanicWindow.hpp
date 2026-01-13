/*
    * PanicWindow.hpp
    * Specialized window for displaying Kernel Panic information
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include "Window.hpp"
#include <Platform/Registers.hpp>

namespace Gui {

    class PanicWindow : public Window {
        System::PanicFrame* m_frame;
        const char* m_message;
        
        static constexpr int PADDING = 10;
        static constexpr int LINE_HEIGHT = 18;

    public:
        // Use a fixed large size for panic window
        PanicWindow(const char* message, System::PanicFrame* frame);

        void Render() override;

    private:
        // Helper to draw a "table row" style line
        void DrawTableRow(int x, int y, int width, const char* label, const char* value, bool header = false);
        void DrawTableRowHex(int x, int y, int width, const char* label, uint64_t value);
    };

}
