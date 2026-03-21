/*
    * app_texteditor.cpp
    * MontaukOS Desktop - Text Editor application
    * Single-buffer text editor with line numbers, cursor, scrolling, file I/O
    * Copyright (c) 2026 Daniel Hammer
*/

#include "apps_common.hpp"

// ============================================================================
// Text Editor state
// ============================================================================

static constexpr int TE_TOOLBAR_H    = 36;
static constexpr int TE_PATHBAR_H    = 32;
static constexpr int TE_STATUS_H     = 24;
static constexpr int TE_LINE_NUM_W   = 48;
static constexpr int TE_SCROLLBAR_W  = 12;
static constexpr int TE_INIT_CAP     = 4096;
static constexpr int TE_MAX_CAP      = 262144;  // 256KB
static constexpr int TE_MAX_LINES    = 16384;
static constexpr int TE_TAB_WIDTH    = 4;

struct TextEditorState {
    char* buffer;
    int buf_len;
    int buf_cap;
    int* line_offsets;
    int line_count;
    int cursor_pos;       // byte position in buffer
    int cursor_line;
    int cursor_col;
    int scroll_y;         // first visible line
    int scroll_x;         // horizontal scroll in pixels
    bool modified;
    bool cursor_moved;    // set when cursor moves, triggers scroll-to-cursor
    char filepath[256];
    char filename[64];
    DesktopState* desktop;

    // Selection
    int sel_anchor;       // byte position where selection started
    int sel_end;          // byte position where selection extends to
    bool has_selection;
    bool mouse_selecting;

    // Scrollbar
    Scrollbar scrollbar;

    bool show_pathbar;
    char pathbar_text[256];
    int pathbar_cursor;
    int pathbar_len;
};

// ============================================================================
// Line index management
// ============================================================================

static void te_recompute_lines(TextEditorState* te) {
    if (!te->line_offsets) {
        te->line_offsets = (int*)montauk::malloc(TE_MAX_LINES * sizeof(int));
    }

    te->line_count = 0;
    te->line_offsets[te->line_count++] = 0;

    for (int i = 0; i < te->buf_len; i++) {
        if (te->buffer[i] == '\n' && te->line_count < TE_MAX_LINES) {
            te->line_offsets[te->line_count++] = i + 1;
        }
    }
}

static void te_update_cursor_pos(TextEditorState* te) {
    // Find which line the cursor is on
    te->cursor_line = 0;
    for (int i = te->line_count - 1; i >= 0; i--) {
        if (te->cursor_pos >= te->line_offsets[i]) {
            te->cursor_line = i;
            break;
        }
    }
    te->cursor_col = te->cursor_pos - te->line_offsets[te->cursor_line];
    te->cursor_moved = true;
}

static int te_line_length(TextEditorState* te, int line) {
    if (line < 0 || line >= te->line_count) return 0;
    int start = te->line_offsets[line];
    int end;
    if (line + 1 < te->line_count) {
        end = te->line_offsets[line + 1] - 1; // exclude newline
    } else {
        end = te->buf_len;
    }
    return end - start;
}

// ============================================================================
// Buffer operations
// ============================================================================

static void te_ensure_capacity(TextEditorState* te, int needed) {
    if (te->buf_len + needed <= te->buf_cap) return;
    int new_cap = te->buf_cap * 2;
    if (new_cap > TE_MAX_CAP) new_cap = TE_MAX_CAP;
    if (new_cap < te->buf_len + needed) new_cap = te->buf_len + needed;
    te->buffer = (char*)montauk::realloc(te->buffer, new_cap);
    te->buf_cap = new_cap;
}

static void te_insert_char(TextEditorState* te, char c) {
    if (te->buf_len >= TE_MAX_CAP - 1) return;
    te_ensure_capacity(te, 1);

    // Shift everything after cursor right
    for (int i = te->buf_len; i > te->cursor_pos; i--) {
        te->buffer[i] = te->buffer[i - 1];
    }
    te->buffer[te->cursor_pos] = c;
    te->buf_len++;
    te->cursor_pos++;
    te->modified = true;

    te_recompute_lines(te);
    te_update_cursor_pos(te);
}

static void te_insert_string(TextEditorState* te, const char* s, int len) {
    if (te->buf_len + len >= TE_MAX_CAP) return;
    te_ensure_capacity(te, len);

    for (int i = te->buf_len - 1; i >= te->cursor_pos; i--) {
        te->buffer[i + len] = te->buffer[i];
    }
    for (int i = 0; i < len; i++) {
        te->buffer[te->cursor_pos + i] = s[i];
    }
    te->buf_len += len;
    te->cursor_pos += len;
    te->modified = true;

    te_recompute_lines(te);
    te_update_cursor_pos(te);
}

static void te_backspace(TextEditorState* te) {
    if (te->cursor_pos <= 0) return;
    te->cursor_pos--;
    for (int i = te->cursor_pos; i < te->buf_len - 1; i++) {
        te->buffer[i] = te->buffer[i + 1];
    }
    te->buf_len--;
    te->modified = true;

    te_recompute_lines(te);
    te_update_cursor_pos(te);
}

static void te_delete_char(TextEditorState* te) {
    if (te->cursor_pos >= te->buf_len) return;
    for (int i = te->cursor_pos; i < te->buf_len - 1; i++) {
        te->buffer[i] = te->buffer[i + 1];
    }
    te->buf_len--;
    te->modified = true;

    te_recompute_lines(te);
    te_update_cursor_pos(te);
}

// ============================================================================
// Cursor movement
// ============================================================================

static void te_move_up(TextEditorState* te) {
    if (te->cursor_line <= 0) return;
    int target_col = te->cursor_col;
    int prev_line = te->cursor_line - 1;
    int prev_len = te_line_length(te, prev_line);
    if (target_col > prev_len) target_col = prev_len;
    te->cursor_pos = te->line_offsets[prev_line] + target_col;
    te_update_cursor_pos(te);
}

static void te_move_down(TextEditorState* te) {
    if (te->cursor_line >= te->line_count - 1) return;
    int target_col = te->cursor_col;
    int next_line = te->cursor_line + 1;
    int next_len = te_line_length(te, next_line);
    if (target_col > next_len) target_col = next_len;
    te->cursor_pos = te->line_offsets[next_line] + target_col;
    te_update_cursor_pos(te);
}

static void te_move_left(TextEditorState* te) {
    if (te->cursor_pos > 0) {
        te->cursor_pos--;
        te_update_cursor_pos(te);
    }
}

static void te_move_right(TextEditorState* te) {
    if (te->cursor_pos < te->buf_len) {
        te->cursor_pos++;
        te_update_cursor_pos(te);
    }
}

static void te_move_home(TextEditorState* te) {
    te->cursor_pos = te->line_offsets[te->cursor_line];
    te_update_cursor_pos(te);
}

static void te_move_end(TextEditorState* te) {
    te->cursor_pos = te->line_offsets[te->cursor_line] + te_line_length(te, te->cursor_line);
    te_update_cursor_pos(te);
}

// ============================================================================
// Selection
// ============================================================================

static void te_clear_selection(TextEditorState* te) {
    te->has_selection = false;
    te->sel_anchor = 0;
    te->sel_end = 0;
}

static void te_sel_range(TextEditorState* te, int* out_start, int* out_end) {
    if (te->sel_anchor < te->sel_end) {
        *out_start = te->sel_anchor;
        *out_end = te->sel_end;
    } else {
        *out_start = te->sel_end;
        *out_end = te->sel_anchor;
    }
}

static void te_start_selection(TextEditorState* te) {
    if (!te->has_selection) {
        te->sel_anchor = te->cursor_pos;
        te->sel_end = te->cursor_pos;
        te->has_selection = true;
    }
}

static void te_update_selection(TextEditorState* te) {
    te->sel_end = te->cursor_pos;
    if (te->sel_anchor == te->sel_end)
        te->has_selection = false;
}

static void te_delete_selection(TextEditorState* te) {
    if (!te->has_selection) return;

    int sel_s, sel_e;
    te_sel_range(te, &sel_s, &sel_e);
    int count = sel_e - sel_s;
    if (count <= 0) { te_clear_selection(te); return; }

    // Remove bytes [sel_s, sel_e) from buffer
    for (int i = sel_s; i < te->buf_len - count; i++)
        te->buffer[i] = te->buffer[i + count];
    te->buf_len -= count;

    te->cursor_pos = sel_s;
    te->modified = true;
    te_clear_selection(te);
    te_recompute_lines(te);
    te_update_cursor_pos(te);
}

// ============================================================================
// Scrolling
// ============================================================================

static void te_ensure_cursor_visible(TextEditorState* te, int visible_lines, int text_area_w) {
    if (te->cursor_line < te->scroll_y) {
        te->scroll_y = te->cursor_line;
    }
    if (te->cursor_line >= te->scroll_y + visible_lines) {
        te->scroll_y = te->cursor_line - visible_lines + 1;
    }

    // Clamp scroll_y
    int max_scroll_y = te->line_count - visible_lines;
    if (max_scroll_y < 0) max_scroll_y = 0;
    if (te->scroll_y > max_scroll_y) te->scroll_y = max_scroll_y;
    if (te->scroll_y < 0) te->scroll_y = 0;

    // Horizontal scroll
    int cell_w = mono_cell_width();
    int cursor_px = te->cursor_col * cell_w;
    int view_w = text_area_w - TE_LINE_NUM_W - TE_SCROLLBAR_W;
    if (cursor_px - te->scroll_x > view_w - cell_w * 2) {
        te->scroll_x = cursor_px - view_w + cell_w * 4;
    }
    if (cursor_px < te->scroll_x) {
        te->scroll_x = cursor_px - cell_w * 2;
        if (te->scroll_x < 0) te->scroll_x = 0;
    }
}

// ============================================================================
// File I/O
// ============================================================================

static void te_load_file(TextEditorState* te, const char* path) {
    int fd = montauk::open(path);
    if (fd < 0) return;

    uint64_t size = montauk::getsize(fd);
    if (size > TE_MAX_CAP) size = TE_MAX_CAP;

    if ((int)size >= te->buf_cap) {
        int new_cap = (int)size + 1024;
        if (new_cap > TE_MAX_CAP) new_cap = TE_MAX_CAP;
        te->buffer = (char*)montauk::realloc(te->buffer, new_cap);
        te->buf_cap = new_cap;
    }

    montauk::read(fd, (uint8_t*)te->buffer, 0, size);
    montauk::close(fd);

    te->buf_len = (int)size;
    te->cursor_pos = 0;
    te->scroll_y = 0;
    te->scroll_x = 0;
    te->modified = false;

    montauk::strncpy(te->filepath, path, 255);

    // Extract filename from path
    int last_slash = -1;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') last_slash = i;
    }
    if (last_slash >= 0) {
        montauk::strncpy(te->filename, path + last_slash + 1, 63);
    } else {
        montauk::strncpy(te->filename, path, 63);
    }

    te_recompute_lines(te);
    te_update_cursor_pos(te);
}

static void te_save_file(TextEditorState* te) {
    if (te->filepath[0] == '\0') return;

    int fd = montauk::fcreate(te->filepath);
    if (fd < 0) return;

    montauk::fwrite(fd, (const uint8_t*)te->buffer, 0, te->buf_len);
    montauk::close(fd);

    te->modified = false;
}

// ============================================================================
// Drawing
// ============================================================================

static void texteditor_on_draw(Window* win, Framebuffer& fb) {
    TextEditorState* te = (TextEditorState*)win->app_data;
    if (!te) return;

    Canvas c(win);
    c.fill(colors::WINDOW_BG);

    int cell_w = mono_cell_width();
    int cell_h = mono_cell_height();

    // ---- Toolbar (36px) ----
    c.fill_rect(0, 0, c.w, TE_TOOLBAR_H, Color::from_rgb(0xF5, 0xF5, 0xF5));

    // Open button
    Color btn_bg = Color::from_rgb(0xE8, 0xE8, 0xE8);
    c.fill_rounded_rect(4, 6, 24, 24, 3, btn_bg);
    if (te->desktop && te->desktop->icon_folder.pixels)
        c.icon(8, 10, te->desktop->icon_folder);

    // Save button
    c.fill_rounded_rect(32, 6, 24, 24, 3, btn_bg);
    if (te->desktop && te->desktop->icon_save.pixels)
        c.icon(36, 10, te->desktop->icon_save);

    // Vertical separator
    c.vline(60, 4, 28, colors::BORDER);

    // Filename + modified flag
    char toolbar_label[128];
    if (te->filename[0]) {
        snprintf(toolbar_label, 128, "%s%s", te->filename, te->modified ? " [modified]" : "");
    } else {
        snprintf(toolbar_label, 128, "Untitled%s", te->modified ? " [modified]" : "");
    }
    int sfh = system_font_height();
    c.text(68, (TE_TOOLBAR_H - sfh) / 2, toolbar_label, colors::TEXT_COLOR);

    // Toolbar bottom separator
    c.hline(0, TE_TOOLBAR_H - 1, c.w, colors::BORDER);

    // ---- Path bar (conditional, 32px) ----
    int editor_y_start = TE_TOOLBAR_H;

    if (te->show_pathbar) {
        int pb_y = TE_TOOLBAR_H;
        c.fill_rect(0, pb_y, c.w, TE_PATHBAR_H, Color::from_rgb(0xF0, 0xF0, 0xF0));

        // Text input box
        int inp_x = 8;
        int inp_y = pb_y + 4;
        int btn_w = 56;
        int inp_w = c.w - inp_x - btn_w - 12;
        int inp_h = 24;

        c.fill_rect(inp_x, inp_y, inp_w, inp_h, colors::WHITE);
        c.rect(inp_x, inp_y, inp_w, inp_h, colors::ACCENT);

        // Path text
        int text_y = inp_y + (inp_h - sfh) / 2;
        c.text(inp_x + 4, text_y, te->pathbar_text, colors::TEXT_COLOR);

        // Blinking cursor in path input
        char prefix[256];
        int plen = te->pathbar_cursor;
        if (plen > 255) plen = 255;
        for (int i = 0; i < plen; i++) prefix[i] = te->pathbar_text[i];
        prefix[plen] = '\0';
        int cx = inp_x + 4 + text_width(prefix);
        c.fill_rect(cx, inp_y + 3, 2, inp_h - 6, colors::ACCENT);

        // Open button
        int ob_x = inp_x + inp_w + 6;
        c.button(ob_x, inp_y, btn_w, inp_h, "Open", colors::ACCENT, colors::WHITE, 3);

        // Path bar bottom separator
        c.hline(0, pb_y + TE_PATHBAR_H - 1, c.w, colors::BORDER);

        editor_y_start = TE_TOOLBAR_H + TE_PATHBAR_H;
    }

    // ---- Editor area ----
    int text_area_h = c.h - editor_y_start - TE_STATUS_H;
    int visible_lines = text_area_h / cell_h;
    if (visible_lines < 1) visible_lines = 1;
    int text_area_w = c.w - TE_SCROLLBAR_W;

    // Only scroll-to-cursor when the cursor actually moved
    if (te->cursor_moved) {
        te_ensure_cursor_visible(te, visible_lines, c.w);
        te->cursor_moved = false;
    }

    // Update scrollbar
    te->scrollbar.bounds = {c.w - TE_SCROLLBAR_W, editor_y_start, TE_SCROLLBAR_W, text_area_h};
    te->scrollbar.content_height = te->line_count * cell_h;
    te->scrollbar.view_height = text_area_h;
    te->scrollbar.scroll_offset = te->scroll_y * cell_h;
    {
        int ms = te->scrollbar.max_scroll();
        if (te->scrollbar.scroll_offset > ms) te->scrollbar.scroll_offset = ms;
        if (te->scrollbar.scroll_offset < 0) te->scrollbar.scroll_offset = 0;
    }

    // Line number gutter background
    c.fill_rect(0, editor_y_start, TE_LINE_NUM_W, text_area_h, Color::from_rgb(0xF0, 0xF0, 0xF0));

    // Gutter separator
    c.vline(TE_LINE_NUM_W, editor_y_start, text_area_h, colors::BORDER);

    // Selection range
    int sel_s = 0, sel_e = 0;
    if (te->has_selection) te_sel_range(te, &sel_s, &sel_e);
    Color sel_bg = Color::from_rgb(0xB0, 0xD0, 0xF0);

    // Draw lines
    Color linenum_color = Color::from_rgb(0x99, 0x99, 0x99);
    Color cursor_line_color = Color::from_rgb(0xFF, 0xFD, 0xE8);
    Color text_color = colors::TEXT_COLOR;

    int text_start_x = TE_LINE_NUM_W + 4;

    for (int vis = 0; vis < visible_lines + 1; vis++) {
        int line = te->scroll_y + vis;
        if (line >= te->line_count) break;

        int py = editor_y_start + vis * cell_h;
        if (py >= editor_y_start + text_area_h) break;

        // Cursor line highlighting (only when no selection)
        if (line == te->cursor_line && !te->has_selection) {
            int hl_h = gui_min(cell_h, editor_y_start + text_area_h - py);
            if (hl_h > 0)
                c.fill_rect(TE_LINE_NUM_W + 1, py, text_area_w - TE_LINE_NUM_W - 1, hl_h, cursor_line_color);
        }

        // Line number
        char num_str[8];
        snprintf(num_str, 8, "%4d", line + 1);
        c.text_mono(4, py, num_str, linenum_color);

        // Line text (per-character rendering with horizontal scroll clipping)
        int line_start = te->line_offsets[line];
        int line_len = te_line_length(te, line);

        for (int ci = 0; ci < line_len; ci++) {
            int px = text_start_x + ci * cell_w - te->scroll_x;
            if (px + cell_w <= TE_LINE_NUM_W + 1) continue;
            if (px >= text_area_w) break;

            int byte_pos = line_start + ci;
            char ch = te->buffer[byte_pos];

            // Draw selection highlight behind text
            if (te->has_selection && byte_pos >= sel_s && byte_pos < sel_e) {
                int hl_h = gui_min(cell_h, editor_y_start + text_area_h - py);
                if (hl_h > 0)
                    c.fill_rect(px, py, cell_w, hl_h, sel_bg);
            }

            if (ch >= 32 || ch < 0) {
                if (fonts::mono && fonts::mono->valid) {
                    GlyphCache* gc = fonts::mono->get_cache(fonts::TERM_SIZE);
                    fonts::mono->draw_char_to_buffer(
                        c.pixels, c.w, c.h, px, py + gc->ascent,
                        ch, text_color, gc);
                } else {
                    // bitmap fallback
                    uint32_t text_px = text_color.to_pixel();
                    const uint8_t* glyph = &font_data[(unsigned char)ch * FONT_HEIGHT];
                    for (int fy = 0; fy < FONT_HEIGHT && py + fy < editor_y_start + text_area_h; fy++) {
                        uint8_t bits = glyph[fy];
                        for (int fx = 0; fx < FONT_WIDTH; fx++) {
                            if (bits & (0x80 >> fx)) {
                                int dx = px + fx;
                                int dy = py + fy;
                                if (dx > TE_LINE_NUM_W && dx < text_area_w && dy >= 0 && dy < c.h)
                                    c.pixels[dy * c.w + dx] = text_px;
                            }
                        }
                    }
                }
            }
        }

        // Draw selection highlight for newline at end of line (visual feedback)
        if (te->has_selection && line + 1 < te->line_count) {
            int nl_pos = line_start + line_len; // the newline byte
            if (nl_pos >= sel_s && nl_pos < sel_e) {
                int px = text_start_x + line_len * cell_w - te->scroll_x;
                if (px > TE_LINE_NUM_W && px < text_area_w) {
                    int hl_h = gui_min(cell_h, editor_y_start + text_area_h - py);
                    if (hl_h > 0)
                        c.fill_rect(px, py, cell_w / 2, hl_h, sel_bg);
                }
            }
        }

        // Draw cursor
        if (line == te->cursor_line) {
            int cx = text_start_x + te->cursor_col * cell_w - te->scroll_x;
            if (cx > TE_LINE_NUM_W && cx + 2 <= text_area_w) {
                int cur_h = gui_min(cell_h, editor_y_start + text_area_h - py);
                if (cur_h > 0)
                    c.fill_rect(cx, py, 2, cur_h, colors::ACCENT);
            }
        }
    }

    // ---- Scrollbar ----
    if (te->scrollbar.content_height > te->scrollbar.view_height) {
        Color sb_fg = (te->scrollbar.hovered || te->scrollbar.dragging)
            ? te->scrollbar.hover_fg : te->scrollbar.fg;
        int sbx = te->scrollbar.bounds.x;
        int sby = te->scrollbar.bounds.y;
        int sbw = te->scrollbar.bounds.w;
        int sbh = te->scrollbar.bounds.h;
        c.fill_rect(sbx, sby, sbw, sbh, colors::SCROLLBAR_BG);
        int th = te->scrollbar.thumb_height();
        int tty = te->scrollbar.thumb_y();
        c.fill_rect(sbx + 1, tty, sbw - 2, th, sb_fg);
    }

    // ---- Status bar ----
    int status_y = c.h - TE_STATUS_H;
    c.fill_rect(0, status_y, c.w, TE_STATUS_H, Color::from_rgb(0x2B, 0x3E, 0x50));

    // Cursor position (right side)
    char status_right[48];
    if (te->has_selection) {
        int ss, se;
        te_sel_range(te, &ss, &se);
        snprintf(status_right, 48, "%d selected  Ln %d, Col %d ", se - ss, te->cursor_line + 1, te->cursor_col + 1);
    } else {
        snprintf(status_right, 48, "Ln %d, Col %d ", te->cursor_line + 1, te->cursor_col + 1);
    }
    int sr_w = text_width(status_right);
    int status_text_y = status_y + (TE_STATUS_H - sfh) / 2;
    c.text(c.w - sr_w - 4, status_text_y, status_right, colors::PANEL_TEXT);

    // Filename + modified flag (left side)
    char status_left[128];
    if (te->filename[0]) {
        snprintf(status_left, 128, " %s%s", te->filename, te->modified ? " [modified]" : "");
    } else {
        snprintf(status_left, 128, " Untitled%s", te->modified ? " [modified]" : "");
    }
    c.text(4, status_text_y, status_left, colors::PANEL_TEXT);
}

// ============================================================================
// Mouse handling
// ============================================================================

static int te_hit_test(TextEditorState* te, int local_x, int local_y, int editor_y_start) {
    int cell_w = mono_cell_width();
    int cell_h = mono_cell_height();

    int clicked_line = te->scroll_y + (local_y - editor_y_start) / cell_h;
    if (clicked_line >= te->line_count) clicked_line = te->line_count - 1;
    if (clicked_line < 0) clicked_line = 0;

    int clicked_col = (local_x - TE_LINE_NUM_W - 4 + te->scroll_x + cell_w / 2) / cell_w;
    if (clicked_col < 0) clicked_col = 0;
    int line_len = te_line_length(te, clicked_line);
    if (clicked_col > line_len) clicked_col = line_len;

    return te->line_offsets[clicked_line] + clicked_col;
}

static void texteditor_on_mouse(Window* win, MouseEvent& ev) {
    TextEditorState* te = (TextEditorState*)win->app_data;
    if (!te) return;

    Rect cr = win->content_rect();
    int local_x = ev.x - cr.x;
    int local_y = ev.y - cr.y;

    int cell_h = mono_cell_height();

    int editor_y_start = TE_TOOLBAR_H + (te->show_pathbar ? TE_PATHBAR_H : 0);
    int text_area_h = cr.h - editor_y_start - TE_STATUS_H;

    // ---- Scrollbar ----
    {
        MouseEvent local_ev = ev;
        local_ev.x = local_x;
        local_ev.y = local_y;
        bool was_dragging = te->scrollbar.dragging;
        te->scrollbar.handle_mouse(local_ev);
        if (te->scrollbar.dragging || was_dragging) {
            // Convert pixel scroll offset back to line index
            if (cell_h > 0)
                te->scroll_y = te->scrollbar.scroll_offset / cell_h;
            return;
        }
    }

    // ---- Toolbar clicks ----
    if (ev.left_pressed() && local_y < TE_TOOLBAR_H) {
        // Open button
        if (local_x >= 4 && local_x < 28 && local_y >= 6 && local_y < 30) {
            te->show_pathbar = !te->show_pathbar;
            if (te->show_pathbar) {
                montauk::strncpy(te->pathbar_text, te->filepath, 255);
                te->pathbar_len = montauk::slen(te->pathbar_text);
                te->pathbar_cursor = te->pathbar_len;
            }
            return;
        }
        // Save button
        if (local_x >= 32 && local_x < 56 && local_y >= 6 && local_y < 30) {
            te_save_file(te);
            return;
        }
        return;
    }

    // ---- Path bar clicks ----
    if (te->show_pathbar && local_y >= TE_TOOLBAR_H && local_y < TE_TOOLBAR_H + TE_PATHBAR_H) {
        if (ev.left_pressed()) {
            int btn_w = 56;
            int inp_w = cr.w - 8 - btn_w - 12;
            int ob_x = 8 + inp_w + 6;
            if (local_x >= ob_x && local_x < ob_x + btn_w) {
                if (te->pathbar_text[0]) {
                    te_load_file(te, te->pathbar_text);
                    char title[64];
                    snprintf(title, 64, "%s - Editor", te->filename);
                    montauk::strncpy(win->title, title, 63);
                    te->show_pathbar = false;
                }
            }
        }
        return;
    }

    // ---- Editor area: mouse press - start selection or place cursor ----
    if (ev.left_pressed() && local_y >= editor_y_start && local_y < editor_y_start + text_area_h
        && local_x > TE_LINE_NUM_W && local_x < cr.w - TE_SCROLLBAR_W) {
        int pos = te_hit_test(te, local_x, local_y, editor_y_start);
        te->cursor_pos = pos;
        te_update_cursor_pos(te);

        te->sel_anchor = pos;
        te->sel_end = pos;
        te->has_selection = false;
        te->mouse_selecting = true;
        return;
    }

    // ---- Editor area: mouse drag - extend selection ----
    if (te->mouse_selecting && ev.left_held() && local_y >= editor_y_start - 20) {
        int pos = te_hit_test(te, local_x, local_y, editor_y_start);
        te->sel_end = pos;
        te->cursor_pos = pos;
        te_update_cursor_pos(te);
        te->has_selection = (te->sel_anchor != te->sel_end);
        return;
    }

    // ---- Mouse release: end selection ----
    if (te->mouse_selecting && ev.left_released()) {
        te->mouse_selecting = false;
        return;
    }

    // ---- Scroll ----
    if (ev.scroll != 0 && local_y >= editor_y_start && local_y < editor_y_start + text_area_h) {
        te->scroll_y -= ev.scroll * 3;
        if (te->scroll_y < 0) te->scroll_y = 0;
        int visible_lines = text_area_h / cell_h;
        int max_scroll = te->line_count - visible_lines;
        if (max_scroll < 0) max_scroll = 0;
        if (te->scroll_y > max_scroll) te->scroll_y = max_scroll;
    }
}

// ============================================================================
// Keyboard handling
// ============================================================================

static void texteditor_on_key(Window* win, const Montauk::KeyEvent& key) {
    TextEditorState* te = (TextEditorState*)win->app_data;
    if (!te || !key.pressed) return;

    // ---- Path bar input mode ----
    if (te->show_pathbar) {
        if (key.ascii == '\n' || key.ascii == '\r') {
            if (te->pathbar_text[0]) {
                te_load_file(te, te->pathbar_text);
                char title[64];
                snprintf(title, 64, "%s - Editor", te->filename);
                montauk::strncpy(win->title, title, 63);
                te->show_pathbar = false;
            }
            return;
        }
        if (key.scancode == 0x01) { // Escape
            te->show_pathbar = false;
            return;
        }
        if (key.ascii == '\b' || key.scancode == 0x0E) {
            if (te->pathbar_cursor > 0) {
                for (int i = te->pathbar_cursor - 1; i < te->pathbar_len - 1; i++)
                    te->pathbar_text[i] = te->pathbar_text[i + 1];
                te->pathbar_len--;
                te->pathbar_cursor--;
                te->pathbar_text[te->pathbar_len] = '\0';
            }
            return;
        }
        if (key.scancode == 0x4B) { // Left
            if (te->pathbar_cursor > 0) te->pathbar_cursor--;
            return;
        }
        if (key.scancode == 0x4D) { // Right
            if (te->pathbar_cursor < te->pathbar_len) te->pathbar_cursor++;
            return;
        }
        if (key.ascii >= 32 && key.ascii < 127 && te->pathbar_len < 254) {
            for (int i = te->pathbar_len; i > te->pathbar_cursor; i--)
                te->pathbar_text[i] = te->pathbar_text[i - 1];
            te->pathbar_text[te->pathbar_cursor] = key.ascii;
            te->pathbar_cursor++;
            te->pathbar_len++;
            te->pathbar_text[te->pathbar_len] = '\0';
            return;
        }
        return;
    }

    // ---- Normal editor mode ----

    // Ctrl+S: save
    if (key.ctrl && (key.ascii == 's' || key.ascii == 'S')) {
        te_save_file(te);
        return;
    }

    // Ctrl+O: open
    if (key.ctrl && (key.ascii == 'o' || key.ascii == 'O')) {
        te->show_pathbar = !te->show_pathbar;
        if (te->show_pathbar) {
            montauk::strncpy(te->pathbar_text, te->filepath, 255);
            te->pathbar_len = montauk::slen(te->pathbar_text);
            te->pathbar_cursor = te->pathbar_len;
        }
        return;
    }

    // Ctrl+A: select all
    if (key.ctrl && (key.ascii == 'a' || key.ascii == 'A')) {
        te->sel_anchor = 0;
        te->sel_end = te->buf_len;
        te->has_selection = (te->buf_len > 0);
        te->cursor_pos = te->buf_len;
        te_update_cursor_pos(te);
        return;
    }

    // Arrow keys (Shift extends selection)
    if (key.scancode == 0x48) { // Up
        if (key.shift) te_start_selection(te);
        else if (te->has_selection) te_clear_selection(te);
        te_move_up(te);
        if (key.shift) te_update_selection(te);
        return;
    }
    if (key.scancode == 0x50) { // Down
        if (key.shift) te_start_selection(te);
        else if (te->has_selection) te_clear_selection(te);
        te_move_down(te);
        if (key.shift) te_update_selection(te);
        return;
    }
    if (key.scancode == 0x4B) { // Left
        if (key.shift) {
            te_start_selection(te);
            te_move_left(te);
            te_update_selection(te);
        } else if (te->has_selection) {
            int s, e; te_sel_range(te, &s, &e);
            te->cursor_pos = s;
            te_update_cursor_pos(te);
            te_clear_selection(te);
        } else {
            te_move_left(te);
        }
        return;
    }
    if (key.scancode == 0x4D) { // Right
        if (key.shift) {
            te_start_selection(te);
            te_move_right(te);
            te_update_selection(te);
        } else if (te->has_selection) {
            int s, e; te_sel_range(te, &s, &e);
            te->cursor_pos = e;
            te_update_cursor_pos(te);
            te_clear_selection(te);
        } else {
            te_move_right(te);
        }
        return;
    }

    // Home
    if (key.scancode == 0x47) {
        if (key.shift) te_start_selection(te);
        else if (te->has_selection) te_clear_selection(te);
        te_move_home(te);
        if (key.shift) te_update_selection(te);
        return;
    }
    // End
    if (key.scancode == 0x4F) {
        if (key.shift) te_start_selection(te);
        else if (te->has_selection) te_clear_selection(te);
        te_move_end(te);
        if (key.shift) te_update_selection(te);
        return;
    }

    // Page Up
    if (key.scancode == 0x49) {
        if (key.shift) te_start_selection(te);
        else if (te->has_selection) te_clear_selection(te);
        int cell_h = mono_cell_height();
        int visible = (te->scrollbar.view_height > 0 && cell_h > 0)
            ? te->scrollbar.view_height / cell_h : 20;
        for (int i = 0; i < visible; i++) te_move_up(te);
        if (key.shift) te_update_selection(te);
        return;
    }
    // Page Down
    if (key.scancode == 0x51) {
        if (key.shift) te_start_selection(te);
        else if (te->has_selection) te_clear_selection(te);
        int cell_h = mono_cell_height();
        int visible = (te->scrollbar.view_height > 0 && cell_h > 0)
            ? te->scrollbar.view_height / cell_h : 20;
        for (int i = 0; i < visible; i++) te_move_down(te);
        if (key.shift) te_update_selection(te);
        return;
    }

    // Delete
    if (key.scancode == 0x53) {
        if (te->has_selection) te_delete_selection(te);
        else te_delete_char(te);
        return;
    }

    // Backspace
    if (key.ascii == '\b' || key.scancode == 0x0E) {
        if (te->has_selection) te_delete_selection(te);
        else te_backspace(te);
        return;
    }

    // Enter
    if (key.ascii == '\n' || key.ascii == '\r') {
        if (te->has_selection) te_delete_selection(te);
        te_insert_char(te, '\n');
        return;
    }

    // Tab
    if (key.ascii == '\t') {
        if (te->has_selection) te_delete_selection(te);
        for (int i = 0; i < TE_TAB_WIDTH; i++) {
            te_insert_char(te, ' ');
        }
        return;
    }

    // Printable characters
    if (key.ascii >= 32 && key.ascii < 127) {
        if (te->has_selection) te_delete_selection(te);
        te_insert_char(te, key.ascii);
        return;
    }
}

static void texteditor_on_close(Window* win) {
    TextEditorState* te = (TextEditorState*)win->app_data;
    if (te) {
        if (te->buffer) montauk::mfree(te->buffer);
        if (te->line_offsets) montauk::mfree(te->line_offsets);
        montauk::mfree(te);
        win->app_data = nullptr;
    }
}

// ============================================================================
// Text Editor launchers
// ============================================================================

void open_texteditor(DesktopState* ds) {
    int idx = desktop_create_window(ds, "Text Editor", 180, 60, 600, 450);
    if (idx < 0) return;

    Window* win = &ds->windows[idx];
    TextEditorState* te = (TextEditorState*)montauk::malloc(sizeof(TextEditorState));
    montauk::memset(te, 0, sizeof(TextEditorState));

    te->buffer = (char*)montauk::malloc(TE_INIT_CAP);
    te->buf_cap = TE_INIT_CAP;
    te->buf_len = 0;
    te->modified = false;
    te->desktop = ds;
    te->show_pathbar = false;
    te->pathbar_text[0] = '\0';
    te->pathbar_cursor = 0;
    te->pathbar_len = 0;
    te->has_selection = false;
    te->mouse_selecting = false;
    te->sel_anchor = 0;
    te->sel_end = 0;
    te->cursor_moved = true;
    te->scrollbar.init(0, 0, TE_SCROLLBAR_W, 100);

    te_recompute_lines(te);
    te_update_cursor_pos(te);

    win->app_data = te;
    win->on_draw = texteditor_on_draw;
    win->on_mouse = texteditor_on_mouse;
    win->on_key = texteditor_on_key;
    win->on_close = texteditor_on_close;
}

void open_texteditor_with_file(DesktopState* ds, const char* path) {
    const char* name = path;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') name = path + i + 1;
    }

    char title[64];
    snprintf(title, 64, "%s - Editor", name);

    int idx = desktop_create_window(ds, title, 180, 60, 600, 450);
    if (idx < 0) return;

    Window* win = &ds->windows[idx];
    TextEditorState* te = (TextEditorState*)montauk::malloc(sizeof(TextEditorState));
    montauk::memset(te, 0, sizeof(TextEditorState));

    te->buffer = (char*)montauk::malloc(TE_INIT_CAP);
    te->buf_cap = TE_INIT_CAP;
    te->buf_len = 0;
    te->modified = false;
    te->desktop = ds;
    te->show_pathbar = false;
    te->pathbar_text[0] = '\0';
    te->pathbar_cursor = 0;
    te->pathbar_len = 0;
    te->has_selection = false;
    te->mouse_selecting = false;
    te->sel_anchor = 0;
    te->sel_end = 0;
    te->scrollbar.init(0, 0, TE_SCROLLBAR_W, 100);

    te_recompute_lines(te);
    te_update_cursor_pos(te);

    te_load_file(te, path);

    win->app_data = te;
    win->on_draw = texteditor_on_draw;
    win->on_mouse = texteditor_on_mouse;
    win->on_key = texteditor_on_key;
    win->on_close = texteditor_on_close;
}
