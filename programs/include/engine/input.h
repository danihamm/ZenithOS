/*
 * input.h
 * MontaukOS 2D Game Engine - Input State
 * Keyboard and mouse state tracking across frames
 * Copyright (c) 2026 Daniel Hammer
 */

#pragma once
#include <cstdint>
#include <montauk/string.h>
#include <Api/Syscall.hpp>

namespace engine {

// Scancode constants for common keys
namespace key {
    static constexpr uint8_t ESC      = 0x01;
    static constexpr uint8_t W        = 0x11;
    static constexpr uint8_t A        = 0x1E;
    static constexpr uint8_t S        = 0x1F;
    static constexpr uint8_t D        = 0x20;
    static constexpr uint8_t E        = 0x12;
    static constexpr uint8_t SPACE    = 0x39;
    static constexpr uint8_t UP       = 0x48;
    static constexpr uint8_t DOWN     = 0x50;
    static constexpr uint8_t LEFT     = 0x4B;
    static constexpr uint8_t RIGHT    = 0x4D;
    static constexpr uint8_t ENTER    = 0x1C;
    static constexpr uint8_t TAB      = 0x0F;
}

struct InputState {
    // Key states: true = currently held
    bool keys[256];
    // Keys pressed this frame (edge-triggered)
    bool keys_pressed[256];

    // Mouse
    int mouse_x = 0;
    int mouse_y = 0;
    int mouse_scroll = 0;
    uint8_t mouse_buttons = 0;
    uint8_t prev_mouse_buttons = 0;

    void init() {
        montauk::memset(keys, 0, sizeof(keys));
        montauk::memset(keys_pressed, 0, sizeof(keys_pressed));
    }

    // Call at the start of each frame to clear per-frame state
    void begin_frame() {
        montauk::memset(keys_pressed, 0, sizeof(keys_pressed));
        mouse_scroll = 0;
        prev_mouse_buttons = mouse_buttons;
    }

    // Process a window event
    void handle_event(const Montauk::WinEvent& ev) {
        if (ev.type == 0) { // keyboard
            // PS/2 scan code set 1: release codes have bit 7 set.
            // Mask to base scancode so press and release map to the same index.
            uint8_t sc = ev.key.scancode & 0x7F;
            if (ev.key.pressed) {
                if (!keys[sc])
                    keys_pressed[sc] = true;
                keys[sc] = true;
            } else {
                keys[sc] = false;
            }
        } else if (ev.type == 1) { // mouse
            mouse_x = ev.mouse.x;
            mouse_y = ev.mouse.y;
            mouse_scroll = ev.mouse.scroll;
            mouse_buttons = ev.mouse.buttons;
        }
    }

    bool key_held(uint8_t scancode) const { return keys[scancode]; }
    bool key_just_pressed(uint8_t scancode) const { return keys_pressed[scancode]; }

    bool mouse_clicked() const {
        return (mouse_buttons & 1) && !(prev_mouse_buttons & 1);
    }
    bool mouse_held() const { return mouse_buttons & 1; }
};

} // namespace engine
