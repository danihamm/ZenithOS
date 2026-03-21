/*
 * edit.cpp
 * Editing, selection, clipboard, formatting, undo/redo, scroll
 * Copyright (c) 2026 Daniel Hammer
 */

#include "spreadsheet.h"

// ============================================================================
// Undo/redo
// ============================================================================

void undo_push() {
    for (int i = g_undo_pos; i < g_undo_count; i++) {
        if (g_undo[i]) { montauk::mfree(g_undo[i]); g_undo[i] = nullptr; }
    }
    if (g_undo[UNDO_MAX]) { montauk::mfree(g_undo[UNDO_MAX]); g_undo[UNDO_MAX] = nullptr; }
    g_undo_count = g_undo_pos;

    if (g_undo_count >= UNDO_MAX) {
        if (g_undo[0]) montauk::mfree(g_undo[0]);
        for (int i = 0; i < UNDO_MAX - 1; i++) g_undo[i] = g_undo[i + 1];
        g_undo[UNDO_MAX - 1] = nullptr;
        g_undo_count = UNDO_MAX - 1;
    }

    UndoEntry* e = (UndoEntry*)montauk::malloc(sizeof(UndoEntry));
    if (!e) return;
    for (int r = 0; r < MAX_ROWS; r++)
        for (int c = 0; c < MAX_COLS; c++) {
            str_cpy(e->cells[r][c].input, g_cells[r][c].input, CELL_TEXT_MAX);
            e->cells[r][c].align = g_cells[r][c].align;
            e->cells[r][c].fmt = g_cells[r][c].fmt;
            e->cells[r][c].bold = g_cells[r][c].bold;
        }
    g_undo[g_undo_count] = e;
    g_undo_count++;
    g_undo_pos = g_undo_count;
}

static void undo_restore(UndoEntry* e) {
    for (int r = 0; r < MAX_ROWS; r++)
        for (int c = 0; c < MAX_COLS; c++) {
            str_cpy(g_cells[r][c].input, e->cells[r][c].input, CELL_TEXT_MAX);
            g_cells[r][c].align = e->cells[r][c].align;
            g_cells[r][c].fmt = e->cells[r][c].fmt;
            g_cells[r][c].bold = e->cells[r][c].bold;
        }
    eval_all_cells();
}

void undo_do() {
    if (g_undo_pos <= 0) return;
    if (g_undo_pos == g_undo_count && !g_undo[g_undo_count]) {
        UndoEntry* e = (UndoEntry*)montauk::malloc(sizeof(UndoEntry));
        if (e) {
            for (int r = 0; r < MAX_ROWS; r++)
                for (int c = 0; c < MAX_COLS; c++) {
                    str_cpy(e->cells[r][c].input, g_cells[r][c].input, CELL_TEXT_MAX);
                    e->cells[r][c].align = g_cells[r][c].align;
                    e->cells[r][c].fmt = g_cells[r][c].fmt;
                    e->cells[r][c].bold = g_cells[r][c].bold;
                }
            g_undo[g_undo_count] = e;
        }
    }
    g_undo_pos--;
    if (g_undo[g_undo_pos]) undo_restore(g_undo[g_undo_pos]);
}

void redo_do() {
    if (g_undo_pos >= g_undo_count) return;
    g_undo_pos++;
    UndoEntry* e = g_undo[g_undo_pos];
    if (e) undo_restore(e);
}

// ============================================================================
// Editing
// ============================================================================

void start_editing() {
    if (g_editing) return;
    g_editing = true;
    Cell* c = &g_cells[g_sel_row][g_sel_col];
    str_cpy(g_edit_buf, c->input, CELL_TEXT_MAX);
    g_edit_len = str_len(g_edit_buf);
    g_edit_cursor = g_edit_len;
}

void commit_edit() {
    if (!g_editing) return;
    g_editing = false;
    undo_push();
    Cell* c = &g_cells[g_sel_row][g_sel_col];
    str_cpy(c->input, g_edit_buf, CELL_TEXT_MAX);
    g_modified = true;
    eval_all_cells();
}

void cancel_edit() {
    g_editing = false;
}

// ============================================================================
// Selection helpers
// ============================================================================

void sel_range(int* c0, int* r0, int* c1, int* r1) {
    if (!g_has_selection) {
        *c0 = *c1 = g_sel_col;
        *r0 = *r1 = g_sel_row;
        return;
    }
    *c0 = g_sel_col < g_anchor_col ? g_sel_col : g_anchor_col;
    *c1 = g_sel_col > g_anchor_col ? g_sel_col : g_anchor_col;
    *r0 = g_sel_row < g_anchor_row ? g_sel_row : g_anchor_row;
    *r1 = g_sel_row > g_anchor_row ? g_sel_row : g_anchor_row;
}

void clear_selection() {
    g_has_selection = false;
    g_anchor_col = g_sel_col;
    g_anchor_row = g_sel_row;
}

void copy_selection() {
    int c0, r0, c1, r1;
    sel_range(&c0, &r0, &c1, &r1);
    g_clip_count = 0;
    g_clip_cols = c1 - c0 + 1;
    g_clip_rows = r1 - r0 + 1;
    for (int r = r0; r <= r1 && g_clip_count < CLIP_MAX_CELLS; r++) {
        for (int c = c0; c <= c1 && g_clip_count < CLIP_MAX_CELLS; c++) {
            ClipCell* cc = &g_clipboard[g_clip_count];
            str_cpy(cc->text, g_cells[r][c].input, CELL_TEXT_MAX);
            cc->rel_col = c - c0;
            cc->rel_row = r - r0;
            cc->align = g_cells[r][c].align;
            cc->fmt = g_cells[r][c].fmt;
            cc->bold = g_cells[r][c].bold;
            g_clip_count++;
        }
    }
}

void cut_selection() {
    copy_selection();
    undo_push();
    int c0, r0, c1, r1;
    sel_range(&c0, &r0, &c1, &r1);
    for (int r = r0; r <= r1; r++)
        for (int c = c0; c <= c1; c++)
            g_cells[r][c].input[0] = '\0';
    eval_all_cells();
    g_modified = true;
}

void paste_at_cursor() {
    if (g_clip_count == 0) return;
    undo_push();
    for (int i = 0; i < g_clip_count; i++) {
        int tc = g_sel_col + g_clipboard[i].rel_col;
        int tr = g_sel_row + g_clipboard[i].rel_row;
        if (tc >= 0 && tc < MAX_COLS && tr >= 0 && tr < MAX_ROWS) {
            str_cpy(g_cells[tr][tc].input, g_clipboard[i].text, CELL_TEXT_MAX);
            g_cells[tr][tc].align = g_clipboard[i].align;
            g_cells[tr][tc].fmt = g_clipboard[i].fmt;
            g_cells[tr][tc].bold = g_clipboard[i].bold;
        }
    }
    eval_all_cells();
    g_modified = true;
}

// ============================================================================
// Formatting helpers (apply to selection)
// ============================================================================

void apply_bold_toggle() {
    undo_push();
    int c0, r0, c1, r1;
    sel_range(&c0, &r0, &c1, &r1);
    bool new_bold = !g_cells[r0][c0].bold;
    for (int r = r0; r <= r1; r++)
        for (int c = c0; c <= c1; c++)
            g_cells[r][c].bold = new_bold;
    g_modified = true;
}

void apply_align(CellAlign a) {
    undo_push();
    int c0, r0, c1, r1;
    sel_range(&c0, &r0, &c1, &r1);
    for (int r = r0; r <= r1; r++)
        for (int c = c0; c <= c1; c++)
            g_cells[r][c].align = a;
    g_modified = true;
}

void apply_format(NumFormat f) {
    undo_push();
    int c0, r0, c1, r1;
    sel_range(&c0, &r0, &c1, &r1);
    for (int r = r0; r <= r1; r++)
        for (int c = c0; c <= c1; c++)
            g_cells[r][c].fmt = f;
    eval_all_cells();
    g_modified = true;
}

// ============================================================================
// Scroll clamping
// ============================================================================

// ============================================================================
// Fill down (fill handle)
// ============================================================================

void fill_down(int src_r0, int src_c0, int src_r1, int src_c1, int dst_r1) {
    if (dst_r1 <= src_r1) return;
    if (dst_r1 >= MAX_ROWS) dst_r1 = MAX_ROWS - 1;

    undo_push();

    int src_rows = src_r1 - src_r0 + 1;

    for (int r = src_r1 + 1; r <= dst_r1; r++) {
        int src_r = src_r0 + ((r - src_r0) % src_rows);
        int drow = r - src_r;
        for (int c = src_c0; c <= src_c1; c++) {
            Cell* src = &g_cells[src_r][c];
            Cell* dst = &g_cells[r][c];
            dst->align = src->align;
            dst->fmt = src->fmt;
            dst->bold = src->bold;

            if (src->input[0] == '=') {
                // Formula: adjust cell references
                adjust_formula_refs(src->input, dst->input, CELL_TEXT_MAX, 0, drow);
            } else {
                str_cpy(dst->input, src->input, CELL_TEXT_MAX);
            }
        }
    }

    eval_all_cells();
    g_modified = true;
}

// ============================================================================
// Auto-fit column width
// ============================================================================

void auto_fit_column(int col) {
    if (!g_font) return;
    int max_w = MIN_COL_W;
    for (int r = 0; r < MAX_ROWS; r++) {
        if (g_cells[r][col].display[0]) {
            TrueTypeFont* f = (g_cells[r][col].bold && g_font_bold) ? g_font_bold : g_font;
            int tw = f->measure_text(g_cells[r][col].display, FONT_SIZE) + 12;
            if (tw > max_w) max_w = tw;
        }
    }
    g_col_widths[col] = max_w;
}

// ============================================================================
// Scroll clamping
// ============================================================================

void clamp_scroll() {
    int pbh = g_pathbar_open ? PATHBAR_H : 0;
    int max_x = content_width() - (g_win_w - ROW_HEADER_W);
    int max_y = content_height() - (g_win_h - TOOLBAR_H - pbh - FORMULA_BAR_H - STATUS_BAR_H);
    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;
    if (g_scroll_x < 0) g_scroll_x = 0;
    if (g_scroll_x > max_x) g_scroll_x = max_x;
    if (g_scroll_y < 0) g_scroll_y = 0;
    if (g_scroll_y > max_y) g_scroll_y = max_y;
}

void ensure_sel_visible() {
    int pbh = g_pathbar_open ? PATHBAR_H : 0;
    int area_w = g_win_w - ROW_HEADER_W;
    int area_h = g_win_h - TOOLBAR_H - pbh - FORMULA_BAR_H - COL_HEADER_H - STATUS_BAR_H;

    int cx = col_x(g_sel_col) - ROW_HEADER_W;
    int cy = g_sel_row * ROW_H;

    if (cx < g_scroll_x) g_scroll_x = cx;
    if (cx + g_col_widths[g_sel_col] > g_scroll_x + area_w)
        g_scroll_x = cx + g_col_widths[g_sel_col] - area_w;

    if (cy < g_scroll_y) g_scroll_y = cy;
    if (cy + ROW_H > g_scroll_y + area_h)
        g_scroll_y = cy + ROW_H - area_h;

    clamp_scroll();
}
