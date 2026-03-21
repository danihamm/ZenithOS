/*
 * render.cpp
 * Rendering -- toolbar, canvas, color bar, status bar
 * Copyright (c) 2026 Daniel Hammer
 */

#include "paint.h"

static const char* tool_names[TOOL_COUNT] = {
    "Pencil", "Brush", "Eraser", "Line",
    "Rect", "FRect", "Ellipse", "FEllip",
    "Fill", "Pick"
};

void render(uint32_t* pixels) {
    px_fill(pixels, g_win_w, g_win_h, 0, 0, g_win_w, g_win_h, BG_COLOR);

    // ================================================================
    // Toolbar
    // ================================================================
    px_fill(pixels, g_win_w, g_win_h, 0, 0, g_win_w, TOOLBAR_H, TOOLBAR_BG);
    px_hline(pixels, g_win_w, g_win_h, 0, TOOLBAR_H - 1, g_win_w, TB_SEP_COLOR);

    int bx = 4;
    auto tb_btn = [&](int w, bool active, const char* label) {
        Color bg = active ? TB_BTN_ACTIVE : TB_BTN_BG;
        px_fill_rounded(pixels, g_win_w, g_win_h, bx, TB_BTN_Y, w, TB_BTN_SIZE, TB_BTN_RAD, bg);
        if (g_font && label[0]) {
            int tw = g_font->measure_text(label, FONT_SIZE);
            g_font->draw_to_buffer(pixels, g_win_w, g_win_h,
                bx + (w - tw) / 2, TB_BTN_Y + (TB_BTN_SIZE - FONT_SIZE) / 2,
                label, HEADER_TEXT, FONT_SIZE);
        }
        bx += w + 4;
    };
    auto tb_sep = [&]() {
        px_vline(pixels, g_win_w, g_win_h, bx, 6, TOOLBAR_H - 12, TB_SEP_COLOR);
        bx += 8;
    };

    // Tool buttons
    for (int i = 0; i < TOOL_COUNT; i++) {
        int w = 48;
        if (i == TOOL_FILLED_RECT || i == TOOL_FILLED_ELLIPSE) w = 48;
        tb_btn(w, g_tool == (Tool)i, tool_names[i]);
        if (i == TOOL_ERASER || i == TOOL_FILLED_ELLIPSE || i == TOOL_EYEDROPPER)
            tb_sep();
    }

    // Brush size indicator
    {
        int bs = brush_size();
        char sz_label[16];
        snprintf(sz_label, 16, "%dpx", bs);
        tb_btn(40, false, sz_label);
    }

    // ================================================================
    // Color bar (below toolbar)
    // ================================================================
    int cbar_y = TOOLBAR_H;
    px_fill(pixels, g_win_w, g_win_h, 0, cbar_y, g_win_w, COLOR_BAR_H, TOOLBAR_BG);
    px_hline(pixels, g_win_w, g_win_h, 0, cbar_y + COLOR_BAR_H - 1, g_win_w, TB_SEP_COLOR);

    // FG/BG color preview
    int preview_x = 4;
    int preview_y = cbar_y + 4;
    int preview_sz = COLOR_BAR_H - 8;

    // BG behind (offset right-down)
    px_fill(pixels, g_win_w, g_win_h, preview_x + 10, preview_y + 6, preview_sz - 4, preview_sz - 4, g_bg_color);
    px_rect(pixels, g_win_w, g_win_h, preview_x + 10, preview_y + 6, preview_sz - 4, preview_sz - 4, Color::from_rgb(0x80, 0x80, 0x80));
    // FG in front (offset left-up)
    px_fill(pixels, g_win_w, g_win_h, preview_x, preview_y, preview_sz - 4, preview_sz - 4, g_fg_color);
    px_rect(pixels, g_win_w, g_win_h, preview_x, preview_y, preview_sz - 4, preview_sz - 4, Color::from_rgb(0x80, 0x80, 0x80));

    // Palette swatches
    int sw_x = preview_x + preview_sz + 16;
    for (int i = 0; i < PALETTE_COUNT; i++) {
        int row = i >= 14 ? 1 : 0;
        int col = i >= 14 ? i - 14 : i;
        int sx = sw_x + col * (SWATCH_SIZE + 2);
        int sy = cbar_y + 2 + row * (SWATCH_SIZE / 2 + 1);
        int sh = SWATCH_SIZE / 2;

        px_fill(pixels, g_win_w, g_win_h, sx, sy, SWATCH_SIZE, sh, PALETTE[i]);

        // Highlight if selected
        bool is_fg = (PALETTE[i].r == g_fg_color.r && PALETTE[i].g == g_fg_color.g && PALETTE[i].b == g_fg_color.b);
        bool is_bg = (PALETTE[i].r == g_bg_color.r && PALETTE[i].g == g_bg_color.g && PALETTE[i].b == g_bg_color.b);
        if (is_fg)
            px_rect(pixels, g_win_w, g_win_h, sx - 1, sy - 1, SWATCH_SIZE + 2, sh + 2, SELECT_BORDER);
        else if (is_bg)
            px_rect(pixels, g_win_w, g_win_h, sx - 1, sy - 1, SWATCH_SIZE + 2, sh + 2, Color::from_rgb(0x99, 0x99, 0x99));
    }

    // ================================================================
    // Canvas area
    // ================================================================
    int area_y = TOOLBAR_H + COLOR_BAR_H;

    // Canvas position (centered in area if smaller, otherwise scrolled)
    int cx = canvas_x();
    int cy = canvas_y();

    // Draw canvas border
    px_rect(pixels, g_win_w, g_win_h, cx - 1, cy - 1, g_canvas_w + 2, g_canvas_h + 2, CANVAS_BORDER);

    // Blit canvas to screen with clipping
    int clip_x0 = 0;
    int clip_y0 = area_y;
    int clip_x1 = g_win_w;
    int clip_y1 = g_win_h - STATUS_BAR_H;

    for (int row = 0; row < g_canvas_h; row++) {
        int sy = cy + row;
        if (sy < clip_y0 || sy >= clip_y1) continue;
        for (int col = 0; col < g_canvas_w; col++) {
            int sx = cx + col;
            if (sx < clip_x0 || sx >= clip_x1) continue;
            pixels[sy * g_win_w + sx] = g_canvas[row * g_canvas_w + col];
        }
    }

    // Shape preview overlay (for line/rect/ellipse tools while dragging)
    if (g_drawing && (g_tool == TOOL_LINE || g_tool == TOOL_RECT || g_tool == TOOL_FILLED_RECT ||
                      g_tool == TOOL_ELLIPSE || g_tool == TOOL_FILLED_ELLIPSE)) {
        // Draw preview directly on screen pixels using canvas coords mapped to screen
        int sx0 = cx + g_draw_x0;
        int sy0 = cy + g_draw_y0;
        int sx1 = cx + g_draw_x1;
        int sy1 = cy + g_draw_y1;

        // Simple preview: draw XOR-style dashed outline
        int rx0 = sx0 < sx1 ? sx0 : sx1;
        int ry0 = sy0 < sy1 ? sy0 : sy1;
        int rx1 = sx0 > sx1 ? sx0 : sx1;
        int ry1 = sy0 > sy1 ? sy0 : sy1;

        Color preview_c = Color::from_rgb(0x80, 0x80, 0x80);
        if (g_tool == TOOL_LINE) {
            // Dashed line preview
            int dx = sx1 - sx0, dy = sy1 - sy0;
            int adx = dx < 0 ? -dx : dx;
            int ady = dy < 0 ? -dy : dy;
            int step_x = dx < 0 ? -1 : 1;
            int step_y = dy < 0 ? -1 : 1;
            int err = adx - ady;
            int lx = sx0, ly = sy0;
            int cnt = 0;
            while (true) {
                if ((cnt / 4) % 2 == 0 &&
                    lx >= clip_x0 && lx < clip_x1 && ly >= clip_y0 && ly < clip_y1)
                    pixels[ly * g_win_w + lx] = preview_c.to_pixel();
                if (lx == sx1 && ly == sy1) break;
                int e2 = 2 * err;
                if (e2 > -ady) { err -= ady; lx += step_x; }
                if (e2 < adx)  { err += adx; ly += step_y; }
                cnt++;
            }
        } else {
            // Dashed rectangle/ellipse outline
            for (int x = rx0; x <= rx1; x++) {
                if (((x - rx0) / 4) % 2 == 0) {
                    if (ry0 >= clip_y0 && ry0 < clip_y1 && x >= clip_x0 && x < clip_x1)
                        pixels[ry0 * g_win_w + x] = preview_c.to_pixel();
                    if (ry1 >= clip_y0 && ry1 < clip_y1 && x >= clip_x0 && x < clip_x1)
                        pixels[ry1 * g_win_w + x] = preview_c.to_pixel();
                }
            }
            for (int y = ry0; y <= ry1; y++) {
                if (((y - ry0) / 4) % 2 == 0) {
                    if (rx0 >= clip_x0 && rx0 < clip_x1 && y >= clip_y0 && y < clip_y1)
                        pixels[y * g_win_w + rx0] = preview_c.to_pixel();
                    if (rx1 >= clip_x0 && rx1 < clip_x1 && y >= clip_y0 && y < clip_y1)
                        pixels[y * g_win_w + rx1] = preview_c.to_pixel();
                }
            }
        }
    }

    // ================================================================
    // Status bar
    // ================================================================
    int sy = g_win_h - STATUS_BAR_H;
    px_fill(pixels, g_win_w, g_win_h, 0, sy, g_win_w, STATUS_BAR_H, STATUS_BG);

    if (g_font) {
        char status[128];
        snprintf(status, 128, " %s - %dx%d", tool_names[g_tool], g_canvas_w, g_canvas_h);
        int sty = sy + (STATUS_BAR_H - FONT_SIZE) / 2;
        g_font->draw_to_buffer(pixels, g_win_w, g_win_h, 6, sty, status, STATUS_TEXT, FONT_SIZE);

        // Right side: cursor position if on canvas
        char right[64];
        if (g_drawing) {
            snprintf(right, 64, "(%d, %d) ", g_draw_x1, g_draw_y1);
        } else {
            snprintf(right, 64, "%s ", g_modified ? "Modified" : "");
        }
        int rw = g_font->measure_text(right, FONT_SIZE);
        g_font->draw_to_buffer(pixels, g_win_w, g_win_h, g_win_w - rw - 6, sty, right, STATUS_TEXT, FONT_SIZE);
    }
}
