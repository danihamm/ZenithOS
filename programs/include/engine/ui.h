/*
 * ui.h
 * MontaukOS 2D Game Engine - UI Rendering
 * Health bars, text overlays, dialog boxes, menus
 * Copyright (c) 2026 Daniel Hammer
 */

#pragma once
#include <cstdint>
#include "engine/engine.h"

extern "C" {
#include <stdio.h>
}

namespace engine {

// ============================================================================
// Health / stat bar
// ============================================================================

inline void draw_bar(Engine& eng, int x, int y, int w, int h,
                     int value, int max_value,
                     uint32_t fill_color, uint32_t bg_color,
                     uint32_t border_color) {
    eng.fill_rect(x, y, w, h, bg_color);
    if (max_value > 0 && value > 0) {
        int fw = (value * (w - 2)) / max_value;
        if (fw < 1) fw = 1;
        eng.fill_rect(x + 1, y + 1, fw, h - 2, fill_color);
    }
    eng.draw_rect_outline(x, y, w, h, border_color);
}

// ============================================================================
// Text box / dialog
// ============================================================================

inline void draw_dialog(Engine& eng, int x, int y, int w, int h,
                        const char* text, gui::Color text_color,
                        int font_size = 16) {
    // Background with border
    eng.fill_rect(x, y, w, h, 0xF0FFFFFF);
    eng.draw_rect_outline(x, y, w, h, 0xFF333333);
    eng.draw_rect_outline(x + 1, y + 1, w - 2, h - 2, 0xFFCCCCCC);

    // Text (with word wrapping)
    int tx = x + 8;
    int ty = y + 6;
    int max_w = w - 16;
    int line_h = eng.text_height(font_size) + 2;

    // Simple word wrap
    char line_buf[128];
    int li = 0;
    const char* p = text;
    while (*p) {
        li = 0;
        while (*p && *p != '\n') {
            // Save state before adding next word
            int word_start = li;
            const char* word_p = p;

            while (*p && *p != ' ' && *p != '\n' && li < 126) {
                line_buf[li++] = *p++;
            }
            line_buf[li] = '\0';

            // Check width
            if (eng.text_width(line_buf, font_size) > max_w && word_start > 0) {
                // Line too long - rewind to before this word
                li = word_start;
                if (li > 0 && line_buf[li - 1] == ' ') li--;
                line_buf[li] = '\0';
                p = word_p; // rewind pointer to retry this word on next line
                break;
            }

            if (*p == ' ') { line_buf[li++] = ' '; p++; }
        }
        line_buf[li] = '\0';
        if (*p == '\n') p++;

        if (ty + line_h > y + h - 4) break;
        eng.draw_text(tx, ty, line_buf, text_color, font_size);
        ty += line_h;
    }
}

// ============================================================================
// Prompt bar (bottom of screen)
// ============================================================================

inline void draw_prompt(Engine& eng, const char* text, int font_size = 14) {
    int bar_h = eng.text_height(font_size) + 8;
    int y = eng.screen_h - bar_h;
    eng.fill_rect_alpha(0, y, eng.screen_w, bar_h, 0xCC000000);
    int tw = eng.text_width(text, font_size);
    eng.draw_text((eng.screen_w - tw) / 2, y + 4, text,
                  gui::Color::from_rgb(0xFF, 0xFF, 0xFF), font_size);
}

// ============================================================================
// Simple HUD text
// ============================================================================

inline void draw_hud_text(Engine& eng, int x, int y,
                          const char* text, gui::Color c, int size = 14) {
    // Draw shadow first for readability
    eng.draw_text(x + 1, y + 1, text, gui::Color::from_rgba(0, 0, 0, 180), size);
    eng.draw_text(x, y, text, c, size);
}

} // namespace engine
