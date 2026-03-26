/*
 * render.cpp
 * Rendering for the standalone MontaukOS Word Processor
 * Copyright (c) 2026 Daniel Hammer
 */

#include "wordprocessor.hpp"

static int wp_ui_line_height() {
    if (g_ui_font && g_ui_font->valid)
        return g_ui_font->get_line_height(fonts::UI_SIZE);
    return fonts::UI_SIZE;
}

static void wp_draw_ui_text(Canvas& c, int x, int y, const char* text, Color color) {
    draw_text(c, g_ui_font, x, y, text, color, fonts::UI_SIZE);
}

static void wp_draw_ui_button(Canvas& c, int x, int y, int w, int h,
                              const char* label, Color bg, Color fg, int radius = 3) {
    c.fill_rounded_rect(x, y, w, h, radius, bg);
    int tw = text_width(g_ui_font, label, fonts::UI_SIZE);
    int th = wp_ui_line_height();
    draw_text(c, g_ui_font, x + (w - tw) / 2, y + (h - th) / 2, label, fg, fonts::UI_SIZE);
}

void wp_render() {
    if (!g_win.pixels) return;

    WordProcessorState* wp = &g_wp;
    Canvas c = g_win.canvas();
    c.fill(colors::WINDOW_BG);

    int sfh = wp_ui_line_height();
    Color toolbar_bg = Color::from_rgb(0xF5, 0xF5, 0xF5);
    Color btn_bg = Color::from_rgb(0xE8, 0xE8, 0xE8);
    Color btn_active = Color::from_rgb(0xC0, 0xD0, 0xE8);

    c.fill_rect(0, 0, c.w, WP_TOOLBAR_H, toolbar_bg);

    c.fill_rounded_rect(4, 6, 24, 24, 3, btn_bg);
    if (g_icon_folder.pixels)
        c.icon(8, 10, g_icon_folder);

    c.fill_rounded_rect(32, 6, 24, 24, 3, btn_bg);
    if (g_icon_save.pixels)
        c.icon(36, 10, g_icon_save);

    c.vline(60, 4, 28, colors::BORDER);

    Color bold_bg = (wp->cur_flags & STYLE_BOLD) ? btn_active : btn_bg;
    c.fill_rounded_rect(66, 6, 24, 24, 3, bold_bg);
    draw_text(c, g_ui_bold ? g_ui_bold : g_ui_font, 73, 8, "B", colors::TEXT_COLOR, fonts::UI_SIZE);

    Color italic_bg = (wp->cur_flags & STYLE_ITALIC) ? btn_active : btn_bg;
    c.fill_rounded_rect(94, 6, 24, 24, 3, italic_bg);
    {
        TrueTypeFont* italic_font = wp_get_font(FONT_ROBOTO, STYLE_ITALIC);
        draw_text(c, italic_font ? italic_font : g_ui_font, 103, 8, "I", colors::TEXT_COLOR, fonts::UI_SIZE);
    }

    c.vline(122, 4, 28, colors::BORDER);

    c.fill_rounded_rect(128, 6, 90, 24, 3, btn_bg);
    wp_draw_ui_text(c, 134, (WP_TOOLBAR_H - sfh) / 2,
                    WP_FONT_NAMES[wp->cur_font_id < FONT_COUNT ? wp->cur_font_id : 0],
                    colors::TEXT_COLOR);

    c.fill_rounded_rect(224, 6, 44, 24, 3, btn_bg);
    {
        char sz[8];
        snprintf(sz, sizeof(sz), "%d", (int)wp->cur_size);
        wp_draw_ui_text(c, 232, (WP_TOOLBAR_H - sfh) / 2, sz, colors::TEXT_COLOR);
    }

    c.vline(272, 4, 28, colors::BORDER);

    c.fill_rounded_rect(278, 6, 24, 24, 3, btn_bg);
    {
        char section[2] = { (char)0xA7, '\0' };
        TrueTypeFont* font = wp_get_font(FONT_ROBOTO, 0);
        draw_text(c, font ? font : g_ui_font, 284, 8, section, colors::TEXT_COLOR, fonts::UI_SIZE);
    }

    {
        char label[128];
        if (wp->filename[0])
            snprintf(label, sizeof(label), "%s%s", wp->filename, wp->modified ? " *" : "");
        else
            snprintf(label, sizeof(label), "Untitled%s", wp->modified ? " *" : "");
        wp_draw_ui_text(c, 312, (WP_TOOLBAR_H - sfh) / 2, label, Color::from_rgb(0x88, 0x88, 0x88));
    }

    c.hline(0, WP_TOOLBAR_H - 1, c.w, colors::BORDER);

    int edit_y = WP_TOOLBAR_H;
    if (wp->show_pathbar) {
        int pb_y = WP_TOOLBAR_H;
        c.fill_rect(0, pb_y, c.w, WP_PATHBAR_H, Color::from_rgb(0xF0, 0xF0, 0xF0));

        int inp_x = 8;
        int inp_y = pb_y + 4;
        int btn_w = 56;
        int inp_w = c.w - inp_x - btn_w - 12;
        int inp_h = 24;

        c.fill_rect(inp_x, inp_y, inp_w, inp_h, colors::WHITE);
        c.rect(inp_x, inp_y, inp_w, inp_h, colors::ACCENT);

        int text_y = inp_y + (inp_h - sfh) / 2;
        wp_draw_ui_text(c, inp_x + 4, text_y, wp->pathbar_text, colors::TEXT_COLOR);

        char prefix[256];
        int plen = wp->pathbar_cursor;
        if (plen > 255) plen = 255;
        for (int i = 0; i < plen; i++) prefix[i] = wp->pathbar_text[i];
        prefix[plen] = '\0';
        int cx = inp_x + 4 + text_width(g_ui_font, prefix, fonts::UI_SIZE);
        c.fill_rect(cx, inp_y + 3, 2, inp_h - 6, colors::ACCENT);

        int btn_x = inp_x + inp_w + 6;
        wp_draw_ui_button(c, btn_x, inp_y, btn_w, inp_h,
                          wp->pathbar_save_mode ? "Save" : "Open",
                          colors::ACCENT, colors::WHITE);

        c.hline(0, pb_y + WP_PATHBAR_H - 1, c.w, colors::BORDER);
        edit_y = WP_TOOLBAR_H + WP_PATHBAR_H;
    }

    int text_area_h = c.h - edit_y - WP_STATUS_H;
    wp_recompute_wrap(wp, c.w);

    wp->scrollbar.bounds = {c.w - WP_SCROLLBAR_W, edit_y, WP_SCROLLBAR_W, text_area_h};
    wp->scrollbar.content_height = wp->content_height;
    wp->scrollbar.view_height = text_area_h;

    {
        int ms = wp->scrollbar.max_scroll();
        if (wp->scrollbar.scroll_offset > ms) wp->scrollbar.scroll_offset = ms;
        if (wp->scrollbar.scroll_offset < 0) wp->scrollbar.scroll_offset = 0;
    }

    wp_ensure_cursor_visible(wp, text_area_h);

    int scroll_y = wp->scrollbar.scroll_offset;
    int cursor_abs = wp_abs_pos(wp, wp->cursor_run, wp->cursor_offset);
    int sel_s = 0;
    int sel_e = 0;
    if (wp->has_selection) wp_sel_range(wp, &sel_s, &sel_e);
    Color sel_bg = Color::from_rgb(0xB0, 0xD0, 0xF0);

    for (int li = 0; li < wp->wrap_line_count; li++) {
        WrapLine* wl = &wp->wrap_lines[li];
        int py = edit_y + wl->y - scroll_y;

        if (py + wl->height <= edit_y) continue;
        if (py >= edit_y + text_area_h) break;

        int chars_left = wl->char_count;
        int ri = wl->run_idx;
        int ro = wl->run_offset;
        int x = WP_MARGIN;
        int line_abs_start = wp_wrap_line_start(wp, li);
        int char_idx = 0;

        while (chars_left > 0 && ri < wp->run_count) {
            StyledRun* r = &wp->runs[ri];
            TrueTypeFont* font = wp_get_font(r->font_id, r->flags);
            if (!font || !font->valid) {
                ri++;
                ro = 0;
                continue;
            }

            GlyphCache* gc = font->get_cache(r->size);
            int baseline = py + wl->baseline;

            int avail = r->len - ro;
            int to_draw = avail < chars_left ? avail : chars_left;

            for (int ci = 0; ci < to_draw; ci++) {
                char ch = r->text[ro + ci];
                int abs_ch = line_abs_start + char_idx;

                int char_adv = 0;
                if (ch != '\n' && (ch >= 32 || ch < 0)) {
                    CachedGlyph* g = font->get_glyph(gc, (unsigned char)ch);
                    char_adv = g ? g->advance : 8;
                }

                if (wp->has_selection && abs_ch >= sel_s && abs_ch < sel_e) {
                    int sel_w = char_adv > 0 ? char_adv : 6;
                    c.fill_rect(x, py, sel_w, wl->height, sel_bg);
                }

                if (abs_ch == cursor_abs) {
                    int cur_h = wl->height;
                    if (py >= edit_y && py + cur_h <= edit_y + text_area_h)
                        c.fill_rect(x, py, 2, cur_h, colors::ACCENT);
                }

                if (ch != '\n' && (ch >= 32 || ch < 0)) {
                    if (x < c.w - WP_SCROLLBAR_W - WP_MARGIN) {
                        Color text_col = (wp->has_selection && abs_ch >= sel_s && abs_ch < sel_e)
                            ? Color::from_rgb(0x10, 0x10, 0x10) : colors::TEXT_COLOR;
                        int adv = font->draw_char_to_buffer(
                            c.pixels, c.w, c.h, x, baseline, (unsigned char)ch, text_col, gc);
                        x += adv;
                    }
                }

                char_idx++;
            }

            chars_left -= to_draw;
            ro += to_draw;
            if (ro >= r->len) {
                ri++;
                ro = 0;
            }
        }

        if (line_abs_start + char_idx == cursor_abs && li == wp->wrap_line_count - 1) {
            int cur_h = wl->height;
            if (py >= edit_y && py + cur_h <= edit_y + text_area_h)
                c.fill_rect(x, py, 2, cur_h, colors::ACCENT);
        }
    }

    if (wp->scrollbar.content_height > wp->scrollbar.view_height) {
        Color sb_fg = (wp->scrollbar.hovered || wp->scrollbar.dragging)
            ? wp->scrollbar.hover_fg : wp->scrollbar.fg;
        int sbx = wp->scrollbar.bounds.x;
        int sby = wp->scrollbar.bounds.y;
        int sbw = wp->scrollbar.bounds.w;
        int sbh = wp->scrollbar.bounds.h;
        c.fill_rect(sbx, sby, sbw, sbh, colors::SCROLLBAR_BG);
        int th = wp->scrollbar.thumb_height();
        int tty = wp->scrollbar.thumb_y();
        c.fill_rect(sbx + 1, tty, sbw - 2, th, sb_fg);
    }

    int status_y = c.h - WP_STATUS_H;
    c.fill_rect(0, status_y, c.w, WP_STATUS_H, Color::from_rgb(0x2B, 0x3E, 0x50));
    int status_text_y = status_y + (WP_STATUS_H - sfh) / 2;

    char status_left[128];
    snprintf(status_left, sizeof(status_left), " %s %dpt  %s%s",
             WP_FONT_NAMES[wp->cur_font_id < FONT_COUNT ? wp->cur_font_id : 0], (int)wp->cur_size,
             (wp->cur_flags & STYLE_BOLD) ? "Bold " : "",
             (wp->cur_flags & STYLE_ITALIC) ? "Italic" : "");
    wp_draw_ui_text(c, 4, status_text_y, status_left, colors::PANEL_TEXT);

    char status_right[32];
    snprintf(status_right, sizeof(status_right), "%d chars ", wp->total_text_len);
    int sr_w = text_width(g_ui_font, status_right, fonts::UI_SIZE);
    wp_draw_ui_text(c, c.w - sr_w - 4, status_text_y, status_right, colors::PANEL_TEXT);

    if (wp->font_dropdown_open) {
        int dx = 128;
        int dy = WP_TOOLBAR_H;
        int dw = 110;
        int dh = FONT_COUNT * 26 + 4;
        c.fill_rect(dx, dy, dw, dh, colors::MENU_BG);
        c.rect(dx, dy, dw, dh, colors::BORDER);
        for (int i = 0; i < FONT_COUNT; i++) {
            int iy = dy + 2 + i * 26;
            if (i == wp->cur_font_id)
                c.fill_rect(dx + 2, iy, dw - 4, 24, colors::MENU_HOVER);
            wp_draw_ui_text(c, dx + 8, iy + (24 - sfh) / 2, WP_FONT_NAMES[i], colors::TEXT_COLOR);
        }
    }

    if (wp->size_dropdown_open) {
        int dx = 224;
        int dy = WP_TOOLBAR_H;
        int dw = 56;
        int dh = WP_SIZE_OPTION_COUNT * 26 + 4;
        c.fill_rect(dx, dy, dw, dh, colors::MENU_BG);
        c.rect(dx, dy, dw, dh, colors::BORDER);
        for (int i = 0; i < WP_SIZE_OPTION_COUNT; i++) {
            int iy = dy + 2 + i * 26;
            if (WP_SIZE_OPTIONS[i] == wp->cur_size)
                c.fill_rect(dx + 2, iy, dw - 4, 24, colors::MENU_HOVER);
            char sz[8];
            snprintf(sz, sizeof(sz), "%d", WP_SIZE_OPTIONS[i]);
            wp_draw_ui_text(c, dx + 8, iy + (24 - sfh) / 2, sz, colors::TEXT_COLOR);
        }
    }
}
