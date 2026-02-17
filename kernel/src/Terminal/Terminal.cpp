/*
    * Terminal.cpp
    * Terminal implementation
    * Copyright (c) 2025 Daniel Hammer
*/

#include "Terminal.hpp"
#include "../Libraries/flanterm/src/flanterm_backends/fb.h"
#include "../Libraries/flanterm/src/flanterm.h"

#include "../Libraries/String.hpp"
#include <CppLib/CString.hpp>

namespace Kt {
    flanterm_context *ctx;
    std::size_t g_terminal_width = 0;

    void UpdatePanelBar(CString panelText) {
        kout << "\033[s";
        kout << "\033[H";

        int panelWidth = g_terminal_width / 9;

        kout << "\033[44m" << "\033[97m";
        kout << panelText;
        for (int i = static_cast<int>(Lib::strlen(panelText)); i < panelWidth; ++i)
            kout << " ";
        kout << "\033[0m";
        kout << "\033[u";
    }

    void Initialize(std::uint32_t *framebuffer, std::size_t width, std::size_t height, std::size_t pitch,
        std::uint8_t red_mask_size, std::uint8_t red_mask_shift,
        std::uint8_t green_mask_size, std::uint8_t green_mask_shift,
        std::uint8_t blue_mask_size, std::uint8_t blue_mask_shift
        )
    {
        ctx = flanterm_fb_init(
            NULL,
            NULL,
            framebuffer,
            width, height, pitch,
            red_mask_size, red_mask_shift,
            green_mask_size, green_mask_shift,
            blue_mask_size, blue_mask_shift,
            NULL,
            NULL, NULL,
            NULL, NULL,
            NULL, NULL,
            NULL, 0, 0, 1,
            0, 0,
            0,
            FLANTERM_FB_ROTATE_0
        );

        g_terminal_width = width;

        UpdatePanelBar("Initializing...");
        kout << "\n\n\n";
    }

    void Putchar(char c) {
        if (c == '\n') {
            flanterm_write(ctx, "\r\n", 2);
            return;
        }
        flanterm_write(ctx, &c, 1);
    }

    void Print(const char *text) {
        for (size_t i = 0; text[i] != '\0'; i++) {
            Putchar(text[i]);
        }
    }

};