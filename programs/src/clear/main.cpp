/*
    * main.cpp
    * clear - Clear terminal screen
    * Copyright (c) 2025-2026 Daniel Hammer
*/

#include <zenith/syscall.h>

extern "C" void _start() {
    zenith::print("\033[2J");   // Clear entire screen
    zenith::print("\033[H");    // Move cursor to top-left
    zenith::exit(0);
}
