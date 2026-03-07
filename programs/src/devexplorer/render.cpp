/*
 * render.cpp
 * MontaukOS Device Explorer — main window rendering
 * Copyright (c) 2026 Daniel Hammer
 */

#include "px.h"

// ============================================================================
// Triangle drawing helpers
// ============================================================================

static void draw_triangle_right(uint32_t* px, int bw, int bh,
                                int x, int y, int size, Color col) {
    uint32_t v = col.to_pixel();
    int half = size / 2;
    for (int row = 0; row < size; row++) {
        int dist = (row <= half) ? row : (size - 1 - row);
        for (int col_px = 0; col_px <= dist; col_px++) {
            int dx = x + col_px, dy = y + row;
            if (dx >= 0 && dx < bw && dy >= 0 && dy < bh)
                px[dy * bw + dx] = v;
        }
    }
}

static void draw_triangle_down(uint32_t* px, int bw, int bh,
                               int x, int y, int size, Color col) {
    uint32_t v = col.to_pixel();
    int half = size / 2;
    for (int row = 0; row < half + 1; row++) {
        int w = size - row * 2;
        for (int col_px = 0; col_px < w; col_px++) {
            int dx = x + row + col_px, dy = y + row;
            if (dx >= 0 && dx < bw && dy >= 0 && dy < bh)
                px[dy * bw + dx] = v;
        }
    }
}

// ============================================================================
// Render: main device list
// ============================================================================

static void render_toolbar(uint32_t* px) {
    auto& de = g_state;
    int fh = font_h();

    px_fill(px, g_win_w, g_win_h, 0, 0, g_win_w, TOOLBAR_H, TOOLBAR_BG);
    px_hline(px, g_win_w, g_win_h, 0, TOOLBAR_H - 1, g_win_w, BORDER_COLOR);

    // "Refresh" button
    int btn_w = 80, btn_h = 26;
    int btn_x = 8;
    int btn_y = (TOOLBAR_H - btn_h) / 2;
    px_button(px, g_win_w, g_win_h, btn_x, btn_y, btn_w, btn_h,
              "Refresh", ACCENT_COLOR, WHITE, 4);

    // Device count on right
    char count_str[24];
    snprintf(count_str, sizeof(count_str), "%d devices", de.dev_count);
    int cw = text_w(count_str);
    px_text(px, g_win_w, g_win_h, g_win_w - cw - 12, (TOOLBAR_H - fh) / 2,
            count_str, TEXT_COLOR);
}

static void render_list(uint32_t* px) {
    auto& de = g_state;
    int fh = font_h();

    DisplayRow rows[MAX_DISPLAY_ROWS];
    int row_count = build_display_rows(&de, rows);

    int list_y = TOOLBAR_H;
    int list_h = g_win_h - list_y;
    if (list_h < 1) return;

    // Compute total content height
    int total_h = 0;
    for (int i = 0; i < row_count; i++)
        total_h += (rows[i].type == ROW_CATEGORY) ? CAT_H : ITEM_H;

    int max_scroll_px = total_h - list_h;
    if (max_scroll_px < 0) max_scroll_px = 0;

    // Compute scroll pixel offset
    int scroll_px = 0;
    for (int i = 0; i < de.scroll_y && i < row_count; i++)
        scroll_px += (rows[i].type == ROW_CATEGORY) ? CAT_H : ITEM_H;
    if (scroll_px > max_scroll_px) {
        scroll_px = max_scroll_px;
        de.scroll_y = 0;
        int acc = 0;
        for (int i = 0; i < row_count; i++) {
            int rh = (rows[i].type == ROW_CATEGORY) ? CAT_H : ITEM_H;
            if (acc + rh > max_scroll_px) break;
            acc += rh;
            de.scroll_y = i + 1;
        }
    }
    if (de.scroll_y < 0) de.scroll_y = 0;
    if (de.selected_row >= row_count) de.selected_row = row_count - 1;

    // Draw rows
    int first_visible = de.scroll_y;
    int cur_y = list_y;
    Color menu_hover = Color::from_rgb(0xD0, 0xE0, 0xF8);

    for (int i = first_visible; i < row_count; i++) {
        int row_h = (rows[i].type == ROW_CATEGORY) ? CAT_H : ITEM_H;
        if (cur_y >= g_win_h) break;

        if (rows[i].type == ROW_CATEGORY) {
            int cat = rows[i].category;
            px_fill(px, g_win_w, g_win_h, 0, cur_y, g_win_w, CAT_H,
                    Color::from_rgb(0xF0, 0xF0, 0xF0));

            if (i == de.selected_row)
                px_fill(px, g_win_w, g_win_h, 0, cur_y, g_win_w, CAT_H, menu_hover);

            // Expand/collapse triangle
            int tri_x = 10;
            int tri_y = cur_y + (CAT_H - 8) / 2;
            Color tri_color = Color::from_rgb(0x55, 0x55, 0x55);
            if (de.collapsed[cat])
                draw_triangle_right(px, g_win_w, g_win_h, tri_x, tri_y, 8, tri_color);
            else
                draw_triangle_down(px, g_win_w, g_win_h, tri_x, tri_y, 8, tri_color);

            // Colored dot
            int dot_x = 24;
            int dot_y = cur_y + (CAT_H - 8) / 2;
            Color cat_col = (cat >= 0 && cat < NUM_CATEGORIES)
                            ? category_colors[cat] : TEXT_COLOR;
            px_fill_rounded(px, g_win_w, g_win_h, dot_x, dot_y, 8, 8, 4, cat_col);

            // Category name
            const char* cat_name = (cat >= 0 && cat < NUM_CATEGORIES)
                                   ? category_names[cat] : "?";
            int text_y = cur_y + (CAT_H - fh) / 2;
            px_text(px, g_win_w, g_win_h, 36, text_y, cat_name,
                    Color::from_rgb(0x33, 0x33, 0x33));

            // Device count
            int cat_count = 0;
            for (int d = 0; d < de.dev_count; d++)
                if (de.devs[d].category == cat) cat_count++;
            char cnt_buf[16];
            snprintf(cnt_buf, sizeof(cnt_buf), "(%d)", cat_count);
            int name_w = text_w(cat_name);
            px_text(px, g_win_w, g_win_h, 36 + name_w + 8, text_y, cnt_buf,
                    Color::from_rgb(0x88, 0x88, 0x88));

            // Bottom border
            px_hline(px, g_win_w, g_win_h, 0, cur_y + CAT_H - 1, g_win_w,
                     Color::from_rgb(0xE0, 0xE0, 0xE0));
        } else {
            int di = rows[i].dev_index;

            if (i == de.selected_row)
                px_fill(px, g_win_w, g_win_h, 0, cur_y, g_win_w, ITEM_H, menu_hover);

            int text_y = cur_y + (ITEM_H - fh) / 2;

            // Device name (indented)
            px_text(px, g_win_w, g_win_h, INDENT + 10, text_y,
                    de.devs[di].name, TEXT_COLOR);

            // Detail string (right column)
            int detail_x = g_win_w / 2 + 20;
            px_text(px, g_win_w, g_win_h, detail_x, text_y,
                    de.devs[di].detail, DIM_TEXT);
        }

        cur_y += row_h;
    }

    // Scrollbar
    if (total_h > list_h) {
        int sb_x = g_win_w - 6;
        int thumb_h = (list_h * list_h) / total_h;
        if (thumb_h < 20) thumb_h = 20;
        int thumb_y = list_y + (scroll_px * (list_h - thumb_h)) / max_scroll_px;
        px_fill(px, g_win_w, g_win_h, sb_x, list_y, 4, list_h,
                Color::from_rgb(0xE0, 0xE0, 0xE0));
        px_fill(px, g_win_w, g_win_h, sb_x, thumb_y, 4, thumb_h,
                Color::from_rgb(0xAA, 0xAA, 0xAA));
    }
}

void render(uint32_t* pixels) {
    px_fill(pixels, g_win_w, g_win_h, 0, 0, g_win_w, g_win_h, BG_COLOR);
    render_toolbar(pixels);
    render_list(pixels);
}

