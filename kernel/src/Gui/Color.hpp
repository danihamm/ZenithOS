/*
    * Color.hpp
    * Color palette definitions for the debug GUI
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <cstdint>

namespace Gui::Colors {

    // Background colors (Catppuccin Mocha inspired)
    constexpr uint32_t Background    = 0x1E1E2E;  // Dark blue-gray (base)
    constexpr uint32_t Surface       = 0x313244;  // Slightly lighter (surface0)
    constexpr uint32_t TopBar        = 0x181825;  // Darker for top bar (mantle)
    constexpr uint32_t Overlay       = 0x45475A;  // Overlay for panels (surface1)

    // Text colors
    constexpr uint32_t Text          = 0xCDD6F4;  // Primary text (text)
    constexpr uint32_t TextDim       = 0x6C7086;  // Dimmed/secondary text (overlay1)
    constexpr uint32_t TextBright    = 0xFFFFFF;  // Bright white

    // Accent colors (for log levels)
    constexpr uint32_t Red           = 0xF38BA8;  // Errors (red)
    constexpr uint32_t Green         = 0xA6E3A1;  // OK/Success (green)
    constexpr uint32_t Yellow        = 0xF9E2AF;  // Warnings (yellow)
    constexpr uint32_t Blue          = 0x89B4FA;  // Info (blue)
    constexpr uint32_t Cyan          = 0x94E2D5;  // Cyan (teal)
    constexpr uint32_t Magenta       = 0xCBA6F7;  // Debug (mauve)

    // UI elements
    constexpr uint32_t Border        = 0x45475A;  // Panel borders (surface1)
    constexpr uint32_t Separator     = 0x313244;  // Separators (surface0)

}
