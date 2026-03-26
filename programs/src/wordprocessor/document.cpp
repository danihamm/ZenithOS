/*
 * document.cpp
 * Document model, wrap layout, font loading, and file I/O
 * Copyright (c) 2026 Daniel Hammer
 */

#include "wordprocessor.hpp"

static void wp_init_run(StyledRun* r, uint8_t font_id, uint8_t size, uint8_t flags) {
    r->cap = 64;
    r->text = (char*)montauk::malloc(r->cap);
    r->text[0] = '\0';
    r->len = 0;
    r->font_id = font_id;
    r->size = size;
    r->flags = flags;
}

static void wp_free_run(StyledRun* r) {
    if (r->text) montauk::mfree(r->text);
    r->text = nullptr;
    r->len = 0;
    r->cap = 0;
}

static void wp_ensure_run_cap(StyledRun* r, int needed) {
    if (r->len + needed <= r->cap) return;
    int new_cap = r->cap * 2;
    if (new_cap < r->len + needed) new_cap = r->len + needed;
    r->text = (char*)montauk::realloc(r->text, new_cap);
    r->cap = new_cap;
}

static bool wp_same_style(const StyledRun* a, uint8_t font_id, uint8_t size, uint8_t flags) {
    return a->font_id == font_id && a->size == size && a->flags == flags;
}

static void wp_font_at(WordProcessorState* wp, int abs_pos,
                       TrueTypeFont** out_font, GlyphCache** out_gc) {
    int r, o;
    wp_pos_to_run(wp, abs_pos, &r, &o);
    if (r >= wp->run_count) r = wp->run_count - 1;
    StyledRun* run = &wp->runs[r];
    *out_font = wp_get_font(run->font_id, run->flags);
    *out_gc = (*out_font && (*out_font)->valid) ? (*out_font)->get_cache(run->size) : nullptr;
}

static void wp_free_runs(WordProcessorState* wp) {
    for (int i = 0; i < wp->run_count; i++)
        wp_free_run(&wp->runs[i]);
    wp->run_count = 0;
    wp->total_text_len = 0;
}

static void wp_merge_adjacent(WordProcessorState* wp) {
    int i = 0;
    while (i < wp->run_count - 1) {
        StyledRun* a = &wp->runs[i];
        StyledRun* b = &wp->runs[i + 1];
        if (a->font_id == b->font_id && a->size == b->size && a->flags == b->flags) {
            int old_a_len = a->len;
            wp_ensure_run_cap(a, b->len);
            montauk::memcpy(a->text + a->len, b->text, b->len);
            a->len += b->len;

            if (wp->cursor_run == i + 1) {
                wp->cursor_run = i;
                wp->cursor_offset = old_a_len + wp->cursor_offset;
            } else if (wp->cursor_run > i + 1) {
                wp->cursor_run--;
            }

            wp_free_run(b);
            for (int j = i + 1; j < wp->run_count - 1; j++)
                wp->runs[j] = wp->runs[j + 1];
            wp->run_count--;
        } else {
            i++;
        }
    }

    i = 0;
    while (i < wp->run_count && wp->run_count > 1) {
        if (wp->runs[i].len == 0) {
            if (wp->cursor_run == i) {
                if (i < wp->run_count - 1) {
                    wp->cursor_run = i;
                    wp->cursor_offset = 0;
                } else {
                    wp->cursor_run = i - 1;
                    wp->cursor_offset = wp->runs[i - 1].len;
                }
            } else if (wp->cursor_run > i) {
                wp->cursor_run--;
            }
            wp_free_run(&wp->runs[i]);
            for (int j = i; j < wp->run_count - 1; j++)
                wp->runs[j] = wp->runs[j + 1];
            wp->run_count--;
        } else {
            i++;
        }
    }
}

static int wp_split_at(WordProcessorState* wp, int abs_pos) {
    int r, o;
    wp_pos_to_run(wp, abs_pos, &r, &o);
    if (o == 0) return r;
    if (o >= wp->runs[r].len) return r + 1;
    if (wp->run_count >= WP_MAX_RUNS) return r;

    StyledRun* src = &wp->runs[r];
    int after_len = src->len - o;

    StyledRun after;
    wp_init_run(&after, src->font_id, src->size, src->flags);
    wp_ensure_run_cap(&after, after_len);
    montauk::memcpy(after.text, src->text + o, after_len);
    after.len = after_len;

    src->len = o;

    for (int i = wp->run_count - 1; i > r; i--)
        wp->runs[i + 1] = wp->runs[i];
    wp->runs[r + 1] = after;
    wp->run_count++;

    if (wp->cursor_run == r && wp->cursor_offset > o) {
        wp->cursor_run = r + 1;
        wp->cursor_offset -= o;
    } else if (wp->cursor_run > r) {
        wp->cursor_run++;
    }

    return r + 1;
}

void wp_load_fonts() {
    if (g_wp_fonts.loaded) return;

    auto load = [](const char* path) -> TrueTypeFont* {
        TrueTypeFont* f = (TrueTypeFont*)montauk::malloc(sizeof(TrueTypeFont));
        montauk::memset(f, 0, sizeof(TrueTypeFont));
        if (!f->init(path)) {
            montauk::mfree(f);
            return nullptr;
        }
        return f;
    };

    g_wp_fonts.fonts[FONT_ROBOTO][0] = fonts::system_font ? fonts::system_font : load("0:/fonts/Roboto-Medium.ttf");
    g_wp_fonts.fonts[FONT_ROBOTO][1] = fonts::system_bold ? fonts::system_bold : load("0:/fonts/Roboto-Bold.ttf");
    g_wp_fonts.fonts[FONT_ROBOTO][2] = load("0:/fonts/Roboto-Italic.ttf");
    g_wp_fonts.fonts[FONT_ROBOTO][3] = load("0:/fonts/Roboto-BoldItalic.ttf");

    g_wp_fonts.fonts[FONT_NOTOSERIF][0] = load("0:/fonts/NotoSerif-Regular.ttf");
    g_wp_fonts.fonts[FONT_NOTOSERIF][1] = load("0:/fonts/NotoSerif-SemiBold.ttf");
    g_wp_fonts.fonts[FONT_NOTOSERIF][2] = load("0:/fonts/NotoSerif-Italic.ttf");
    g_wp_fonts.fonts[FONT_NOTOSERIF][3] = load("0:/fonts/NotoSerif-BoldItalic.ttf");

    g_wp_fonts.fonts[FONT_C059][0] = load("0:/fonts/C059-Roman.ttf");
    g_wp_fonts.fonts[FONT_C059][1] = load("0:/fonts/C059-Bold.ttf");
    g_wp_fonts.fonts[FONT_C059][2] = load("0:/fonts/C059-Italic.ttf");
    g_wp_fonts.fonts[FONT_C059][3] = load("0:/fonts/C059-Bold.ttf");

    g_ui_font = g_wp_fonts.fonts[FONT_ROBOTO][0];
    g_ui_bold = g_wp_fonts.fonts[FONT_ROBOTO][1] ? g_wp_fonts.fonts[FONT_ROBOTO][1] : g_ui_font;
    g_wp_fonts.loaded = true;
}

TrueTypeFont* wp_get_font(int font_id, uint8_t flags) {
    if (font_id < 0 || font_id >= FONT_COUNT) font_id = 0;

    int variant = 0;
    if ((flags & STYLE_BOLD) && (flags & STYLE_ITALIC)) variant = 3;
    else if (flags & STYLE_BOLD) variant = 1;
    else if (flags & STYLE_ITALIC) variant = 2;

    TrueTypeFont* f = g_wp_fonts.fonts[font_id][variant];
    if (f && f->valid) return f;

    f = g_wp_fonts.fonts[font_id][0];
    if (f && f->valid) return f;

    return g_ui_font ? g_ui_font : fonts::system_font;
}

void wp_init_empty_document(WordProcessorState* wp) {
    wp_init_run(&wp->runs[0], FONT_ROBOTO, WP_DEFAULT_SIZE, 0);
    wp->run_count = 1;
    wp->total_text_len = 0;

    wp->cursor_run = 0;
    wp->cursor_offset = 0;

    wp->sel_anchor = 0;
    wp->sel_end = 0;
    wp->has_selection = false;
    wp->mouse_selecting = false;

    wp->cur_font_id = FONT_ROBOTO;
    wp->cur_size = WP_DEFAULT_SIZE;
    wp->cur_flags = 0;

    wp->scrollbar.init(0, 0, WP_SCROLLBAR_W, 100);
    wp->content_height = 0;

    wp->wrap_lines = nullptr;
    wp->wrap_line_count = 0;
    wp->wrap_dirty = true;
    wp->last_wrap_width = 0;

    wp->modified = false;
    wp->filepath[0] = '\0';
    wp->filename[0] = '\0';

    wp->show_pathbar = false;
    wp->pathbar_save_mode = false;
    wp->pathbar_text[0] = '\0';
    wp->pathbar_cursor = 0;
    wp->pathbar_len = 0;

    wp->font_dropdown_open = false;
    wp->size_dropdown_open = false;
}

void wp_free_document(WordProcessorState* wp) {
    wp_free_runs(wp);
    if (wp->wrap_lines) {
        montauk::mfree(wp->wrap_lines);
        wp->wrap_lines = nullptr;
    }
    wp->wrap_line_count = 0;
}

int wp_abs_pos(WordProcessorState* wp, int run, int offset) {
    int pos = 0;
    for (int i = 0; i < run && i < wp->run_count; i++)
        pos += wp->runs[i].len;
    pos += offset;
    return pos;
}

void wp_pos_to_run(WordProcessorState* wp, int abs_pos, int* out_run, int* out_offset) {
    if (abs_pos <= 0) {
        *out_run = 0;
        *out_offset = 0;
        return;
    }

    int pos = 0;
    for (int i = 0; i < wp->run_count; i++) {
        if (pos + wp->runs[i].len >= abs_pos || i == wp->run_count - 1) {
            *out_run = i;
            *out_offset = abs_pos - pos;
            if (*out_offset > wp->runs[i].len) *out_offset = wp->runs[i].len;
            return;
        }
        pos += wp->runs[i].len;
    }

    *out_run = 0;
    *out_offset = 0;
}

char wp_char_at(WordProcessorState* wp, int abs_pos) {
    int r, o;
    wp_pos_to_run(wp, abs_pos, &r, &o);
    if (r < wp->run_count && o < wp->runs[r].len)
        return wp->runs[r].text[o];
    return '\0';
}

void wp_insert_char(WordProcessorState* wp, char c) {
    if (wp->total_text_len >= WP_MAX_TEXT - 1) return;

    StyledRun* cur = &wp->runs[wp->cursor_run];
    if (wp_same_style(cur, wp->cur_font_id, wp->cur_size, wp->cur_flags)) {
        wp_ensure_run_cap(cur, 1);
        for (int i = cur->len; i > wp->cursor_offset; i--)
            cur->text[i] = cur->text[i - 1];
        cur->text[wp->cursor_offset] = c;
        cur->len++;
        wp->cursor_offset++;
        wp->total_text_len++;
        wp->modified = true;
        wp->wrap_dirty = true;
        return;
    }

    if (wp->cursor_offset == 0) {
        if (wp->run_count >= WP_MAX_RUNS) return;
        for (int i = wp->run_count; i > wp->cursor_run; i--)
            wp->runs[i] = wp->runs[i - 1];
        wp->run_count++;
        StyledRun* nr = &wp->runs[wp->cursor_run];
        wp_init_run(nr, wp->cur_font_id, wp->cur_size, wp->cur_flags);
        wp_ensure_run_cap(nr, 1);
        nr->text[0] = c;
        nr->len = 1;
        wp->cursor_offset = 1;
        wp->total_text_len++;
    } else if (wp->cursor_offset == cur->len) {
        if (wp->run_count >= WP_MAX_RUNS) return;
        int insert_idx = wp->cursor_run + 1;
        for (int i = wp->run_count; i > insert_idx; i--)
            wp->runs[i] = wp->runs[i - 1];
        wp->run_count++;
        StyledRun* nr = &wp->runs[insert_idx];
        wp_init_run(nr, wp->cur_font_id, wp->cur_size, wp->cur_flags);
        wp_ensure_run_cap(nr, 1);
        nr->text[0] = c;
        nr->len = 1;
        wp->cursor_run = insert_idx;
        wp->cursor_offset = 1;
        wp->total_text_len++;
    } else {
        if (wp->run_count + 2 > WP_MAX_RUNS) return;
        int split_at = wp->cursor_offset;
        int after_len = cur->len - split_at;

        StyledRun after;
        wp_init_run(&after, cur->font_id, cur->size, cur->flags);
        wp_ensure_run_cap(&after, after_len);
        montauk::memcpy(after.text, cur->text + split_at, after_len);
        after.len = after_len;

        cur->len = split_at;

        int new_idx = wp->cursor_run + 1;
        for (int i = wp->run_count - 1; i >= new_idx; i--)
            wp->runs[i + 2] = wp->runs[i];

        StyledRun* nr = &wp->runs[new_idx];
        wp_init_run(nr, wp->cur_font_id, wp->cur_size, wp->cur_flags);
        wp_ensure_run_cap(nr, 1);
        nr->text[0] = c;
        nr->len = 1;

        wp->runs[new_idx + 1] = after;
        wp->run_count += 2;
        wp->cursor_run = new_idx;
        wp->cursor_offset = 1;
        wp->total_text_len++;
    }

    wp->modified = true;
    wp->wrap_dirty = true;
}

void wp_backspace(WordProcessorState* wp) {
    if (wp->cursor_run == 0 && wp->cursor_offset == 0) return;

    if (wp->cursor_offset > 0) {
        StyledRun* r = &wp->runs[wp->cursor_run];
        for (int i = wp->cursor_offset - 1; i < r->len - 1; i++)
            r->text[i] = r->text[i + 1];
        r->len--;
        wp->cursor_offset--;
    } else {
        wp->cursor_run--;
        StyledRun* r = &wp->runs[wp->cursor_run];
        if (r->len > 0) r->len--;
        wp->cursor_offset = r->len;
    }

    wp->total_text_len--;
    wp->modified = true;
    wp->wrap_dirty = true;
    wp_merge_adjacent(wp);
}

void wp_delete_char(WordProcessorState* wp) {
    int abs = wp_abs_pos(wp, wp->cursor_run, wp->cursor_offset);
    if (abs >= wp->total_text_len) return;

    StyledRun* r = &wp->runs[wp->cursor_run];
    if (wp->cursor_offset < r->len) {
        for (int i = wp->cursor_offset; i < r->len - 1; i++)
            r->text[i] = r->text[i + 1];
        r->len--;
    } else if (wp->cursor_run + 1 < wp->run_count) {
        StyledRun* next = &wp->runs[wp->cursor_run + 1];
        if (next->len > 0) {
            for (int i = 0; i < next->len - 1; i++)
                next->text[i] = next->text[i + 1];
            next->len--;
        }
    }

    wp->total_text_len--;
    wp->modified = true;
    wp->wrap_dirty = true;
    wp_merge_adjacent(wp);
}

void wp_cursor_left(WordProcessorState* wp) {
    if (wp->cursor_offset > 0) {
        wp->cursor_offset--;
    } else if (wp->cursor_run > 0) {
        wp->cursor_run--;
        wp->cursor_offset = wp->runs[wp->cursor_run].len;
    }
}

void wp_cursor_right(WordProcessorState* wp) {
    if (wp->cursor_offset < wp->runs[wp->cursor_run].len) {
        wp->cursor_offset++;
    } else if (wp->cursor_run + 1 < wp->run_count) {
        wp->cursor_run++;
        wp->cursor_offset = 0;
    }
}

void wp_clear_selection(WordProcessorState* wp) {
    wp->has_selection = false;
    wp->sel_anchor = 0;
    wp->sel_end = 0;
}

void wp_sel_range(WordProcessorState* wp, int* out_start, int* out_end) {
    if (wp->sel_anchor < wp->sel_end) {
        *out_start = wp->sel_anchor;
        *out_end = wp->sel_end;
    } else {
        *out_start = wp->sel_end;
        *out_end = wp->sel_anchor;
    }
}

void wp_start_selection(WordProcessorState* wp) {
    if (!wp->has_selection) {
        wp->sel_anchor = wp_abs_pos(wp, wp->cursor_run, wp->cursor_offset);
        wp->sel_end = wp->sel_anchor;
        wp->has_selection = true;
    }
}

void wp_update_selection_to_cursor(WordProcessorState* wp) {
    wp->sel_end = wp_abs_pos(wp, wp->cursor_run, wp->cursor_offset);
    if (wp->sel_anchor == wp->sel_end) wp->has_selection = false;
}

void wp_apply_style_to_selection(WordProcessorState* wp, int mode, int value) {
    if (!wp->has_selection) return;

    int sel_s, sel_e;
    wp_sel_range(wp, &sel_s, &sel_e);
    if (sel_s >= sel_e) return;

    int end_ri = wp_split_at(wp, sel_e);
    int start_ri = wp_split_at(wp, sel_s);

    {
        int pos = 0;
        end_ri = wp->run_count;
        for (int i = 0; i < wp->run_count; i++) {
            if (pos >= sel_e) {
                end_ri = i;
                break;
            }
            pos += wp->runs[i].len;
        }
    }

    for (int i = start_ri; i < end_ri && i < wp->run_count; i++) {
        StyledRun* r = &wp->runs[i];
        switch (mode) {
        case 0: r->font_id = (uint8_t)value; break;
        case 1: r->size = (uint8_t)value; break;
        case 2: r->flags ^= STYLE_BOLD; break;
        case 3: r->flags ^= STYLE_ITALIC; break;
        }
    }

    wp->wrap_dirty = true;
    wp->modified = true;
    wp_merge_adjacent(wp);
}

void wp_delete_selection(WordProcessorState* wp) {
    if (!wp->has_selection) return;

    int sel_s, sel_e;
    wp_sel_range(wp, &sel_s, &sel_e);
    int count = sel_e - sel_s;
    if (count <= 0) {
        wp_clear_selection(wp);
        return;
    }

    wp_pos_to_run(wp, sel_s, &wp->cursor_run, &wp->cursor_offset);
    for (int i = 0; i < count; i++)
        wp_delete_char(wp);

    wp_clear_selection(wp);
}

void wp_recompute_wrap(WordProcessorState* wp, int content_w) {
    int wrap_width = content_w - WP_MARGIN * 2 - WP_SCROLLBAR_W;
    if (wrap_width < 50) wrap_width = 50;

    if (wrap_width != wp->last_wrap_width) {
        wp->wrap_dirty = true;
        wp->last_wrap_width = wrap_width;
    }

    if (!wp->wrap_dirty && wp->wrap_lines) return;

    if (!wp->wrap_lines)
        wp->wrap_lines = (WrapLine*)montauk::malloc(WP_MAX_WRAP_LINES * sizeof(WrapLine));

    wp->wrap_line_count = 0;
    int total = wp->total_text_len;
    int y = WP_MARGIN;
    int pos = 0;

    while (pos < total && wp->wrap_line_count < WP_MAX_WRAP_LINES) {
        WrapLine* line = &wp->wrap_lines[wp->wrap_line_count];
        wp_pos_to_run(wp, pos, &line->run_idx, &line->run_offset);
        line->char_count = 0;
        line->y = y;

        int x = 0;
        int last_space = -1;
        int line_start = pos;
        int max_ascent = 0;
        int max_height = 0;

        int scan = pos;
        while (scan < total) {
            char ch = wp_char_at(wp, scan);

            TrueTypeFont* font;
            GlyphCache* gc;
            wp_font_at(wp, scan, &font, &gc);
            if (gc) {
                if (gc->ascent > max_ascent) max_ascent = gc->ascent;
                if (gc->line_height > max_height) max_height = gc->line_height;
            }

            if (ch == '\n') {
                scan++;
                break;
            }

            int cw = 8;
            if (font && gc) {
                CachedGlyph* g = font->get_glyph(gc, (unsigned char)ch);
                cw = g ? g->advance : 8;
            }

            if (x + cw > wrap_width && scan > line_start) {
                if (last_space >= line_start) scan = last_space + 1;
                break;
            }

            if (ch == ' ') last_space = scan;
            x += cw;
            scan++;
        }

        line->char_count = scan - line_start;

        if (max_height == 0) {
            TrueTypeFont* df = wp_get_font(wp->cur_font_id, 0);
            if (df && df->valid) {
                GlyphCache* dgc = df->get_cache(wp->cur_size);
                max_height = dgc->line_height;
                max_ascent = dgc->ascent;
            } else {
                max_height = WP_DEFAULT_SIZE;
                max_ascent = WP_DEFAULT_SIZE;
            }
        }

        max_ascent = 0;
        max_height = 0;
        {
            int ri, ro;
            wp_pos_to_run(wp, line_start, &ri, &ro);
            int left = line->char_count;
            while (left > 0 && ri < wp->run_count) {
                StyledRun* r = &wp->runs[ri];
                TrueTypeFont* font = wp_get_font(r->font_id, r->flags);
                if (font && font->valid) {
                    GlyphCache* gc = font->get_cache(r->size);
                    if (gc->ascent > max_ascent) max_ascent = gc->ascent;
                    if (gc->line_height > max_height) max_height = gc->line_height;
                }
                int avail = r->len - ro;
                int consume = avail < left ? avail : left;
                left -= consume;
                ro += consume;
                if (ro >= r->len) {
                    ri++;
                    ro = 0;
                }
            }
            if (max_height == 0) {
                TrueTypeFont* df = wp_get_font(wp->cur_font_id, 0);
                if (df && df->valid) {
                    GlyphCache* dgc = df->get_cache(wp->cur_size);
                    max_height = dgc->line_height;
                    max_ascent = dgc->ascent;
                } else {
                    max_height = WP_DEFAULT_SIZE;
                    max_ascent = WP_DEFAULT_SIZE;
                }
            }
        }

        line->height = max_height;
        line->baseline = max_ascent;
        wp->wrap_line_count++;

        y += max_height;
        pos = line_start + line->char_count;

        if (line->char_count == 0 && pos < total)
            pos++;
    }

    if (total > 0 && wp_char_at(wp, total - 1) == '\n' && wp->wrap_line_count < WP_MAX_WRAP_LINES) {
        WrapLine* line = &wp->wrap_lines[wp->wrap_line_count];
        wp_pos_to_run(wp, total, &line->run_idx, &line->run_offset);
        line->char_count = 0;
        line->y = y;

        TrueTypeFont* df = wp_get_font(wp->cur_font_id, wp->cur_flags);
        if (df && df->valid) {
            GlyphCache* dgc = df->get_cache(wp->cur_size);
            line->height = dgc->line_height;
            line->baseline = dgc->ascent;
        } else {
            line->height = WP_DEFAULT_SIZE;
            line->baseline = WP_DEFAULT_SIZE;
        }

        wp->wrap_line_count++;
        y += line->height;
    }

    if (wp->wrap_line_count == 0) {
        WrapLine* line = &wp->wrap_lines[0];
        line->run_idx = 0;
        line->run_offset = 0;
        line->char_count = 0;
        line->y = WP_MARGIN;

        TrueTypeFont* df = wp_get_font(wp->cur_font_id, 0);
        if (df && df->valid) {
            GlyphCache* dgc = df->get_cache(wp->cur_size);
            line->height = dgc->line_height;
            line->baseline = dgc->ascent;
        } else {
            line->height = WP_DEFAULT_SIZE;
            line->baseline = WP_DEFAULT_SIZE;
        }

        wp->wrap_line_count = 1;
        y = WP_MARGIN + line->height;
    }

    wp->content_height = y + WP_MARGIN;
    wp->wrap_dirty = false;
}

int wp_find_wrap_line(WordProcessorState* wp, int abs_pos) {
    int pos = 0;
    for (int i = 0; i < wp->wrap_line_count; i++) {
        int next_pos = pos + wp->wrap_lines[i].char_count;
        if (abs_pos < next_pos || i == wp->wrap_line_count - 1)
            return i;
        pos = next_pos;
    }
    return 0;
}

int wp_wrap_line_start(WordProcessorState* wp, int line_idx) {
    int pos = 0;
    for (int i = 0; i < line_idx && i < wp->wrap_line_count; i++)
        pos += wp->wrap_lines[i].char_count;
    return pos;
}

void wp_cursor_up(WordProcessorState* wp) {
    int abs = wp_abs_pos(wp, wp->cursor_run, wp->cursor_offset);
    int line = wp_find_wrap_line(wp, abs);
    if (line <= 0) return;

    int col = abs - wp_wrap_line_start(wp, line);
    int prev_start = wp_wrap_line_start(wp, line - 1);
    int prev_len = wp->wrap_lines[line - 1].char_count;
    int new_col = col < prev_len ? col : prev_len;
    if (new_col > 0 && new_col == prev_len) {
        char ch = wp_char_at(wp, prev_start + prev_len - 1);
        if (ch == '\n') new_col = prev_len - 1;
    }
    wp_pos_to_run(wp, prev_start + new_col, &wp->cursor_run, &wp->cursor_offset);
}

void wp_cursor_down(WordProcessorState* wp) {
    int abs = wp_abs_pos(wp, wp->cursor_run, wp->cursor_offset);
    int line = wp_find_wrap_line(wp, abs);
    if (line >= wp->wrap_line_count - 1) return;

    int col = abs - wp_wrap_line_start(wp, line);
    int next_start = wp_wrap_line_start(wp, line + 1);
    int next_len = wp->wrap_lines[line + 1].char_count;
    int new_col = col < next_len ? col : next_len;
    if (new_col > 0 && new_col == next_len && line + 1 < wp->wrap_line_count - 1) {
        char ch = wp_char_at(wp, next_start + next_len - 1);
        if (ch == '\n') new_col = next_len - 1;
    }
    wp_pos_to_run(wp, next_start + new_col, &wp->cursor_run, &wp->cursor_offset);
}

void wp_ensure_cursor_visible(WordProcessorState* wp, int view_h) {
    int abs = wp_abs_pos(wp, wp->cursor_run, wp->cursor_offset);
    int line = wp_find_wrap_line(wp, abs);
    if (line < 0 || line >= wp->wrap_line_count) return;

    int cy = wp->wrap_lines[line].y;
    int ch = wp->wrap_lines[line].height;

    if (cy < wp->scrollbar.scroll_offset) {
        wp->scrollbar.scroll_offset = cy - WP_MARGIN;
        if (wp->scrollbar.scroll_offset < 0) wp->scrollbar.scroll_offset = 0;
    }
    if (cy + ch > wp->scrollbar.scroll_offset + view_h) {
        wp->scrollbar.scroll_offset = cy + ch - view_h + WP_MARGIN;
    }

    int ms = wp->scrollbar.max_scroll();
    if (wp->scrollbar.scroll_offset > ms) wp->scrollbar.scroll_offset = ms;
    if (wp->scrollbar.scroll_offset < 0) wp->scrollbar.scroll_offset = 0;
}

void wp_set_filepath(WordProcessorState* wp, const char* path) {
    montauk::strncpy(wp->filepath, path, 255);
    int last_slash = -1;
    for (int i = 0; path[i]; i++)
        if (path[i] == '/') last_slash = i;
    if (last_slash >= 0)
        montauk::strncpy(wp->filename, path + last_slash + 1, 63);
    else
        montauk::strncpy(wp->filename, path, 63);
}

void wp_open_save_pathbar(WordProcessorState* wp) {
    wp->show_pathbar = true;
    wp->pathbar_save_mode = true;
    montauk::strncpy(wp->pathbar_text, wp->filepath, 255);
    wp->pathbar_len = montauk::slen(wp->pathbar_text);
    wp->pathbar_cursor = wp->pathbar_len;
}

void wp_save_file(WordProcessorState* wp) {
    if (wp->filepath[0] == '\0') {
        wp_open_save_pathbar(wp);
        return;
    }

    int size = 4 + 2 + 1 + 1 + 2;
    for (int i = 0; i < wp->run_count; i++)
        size += 2 + 1 + 1 + 1 + 1 + wp->runs[i].len;

    uint8_t* buf = (uint8_t*)montauk::malloc(size);
    int off = 0;

    buf[off++] = 'M'; buf[off++] = 'W'; buf[off++] = 'P'; buf[off++] = '1';
    buf[off++] = 1; buf[off++] = 0;
    buf[off++] = wp->cur_font_id;
    buf[off++] = wp->cur_size;
    buf[off++] = (uint8_t)(wp->run_count & 0xFF);
    buf[off++] = (uint8_t)((wp->run_count >> 8) & 0xFF);

    for (int i = 0; i < wp->run_count; i++) {
        StyledRun* r = &wp->runs[i];
        buf[off++] = (uint8_t)(r->len & 0xFF);
        buf[off++] = (uint8_t)((r->len >> 8) & 0xFF);
        buf[off++] = r->font_id;
        buf[off++] = r->size;
        buf[off++] = r->flags;
        buf[off++] = 0;
        montauk::memcpy(buf + off, r->text, r->len);
        off += r->len;
    }

    int fd = montauk::fcreate(wp->filepath);
    if (fd >= 0) {
        montauk::fwrite(fd, buf, 0, off);
        montauk::close(fd);
        wp->modified = false;
    }

    montauk::mfree(buf);
}

void wp_load_file(WordProcessorState* wp, const char* path) {
    int fd = montauk::open(path);
    if (fd < 0) return;

    uint64_t fsize = montauk::getsize(fd);
    if (fsize < 10 || fsize > WP_MAX_TEXT * 2) {
        montauk::close(fd);
        return;
    }

    uint8_t* buf = (uint8_t*)montauk::malloc((int)fsize);
    montauk::read(fd, buf, 0, fsize);
    montauk::close(fd);

    if (buf[0] != 'M' || buf[1] != 'W' || buf[2] != 'P' || buf[3] != '1') {
        montauk::mfree(buf);
        return;
    }

    wp_free_runs(wp);

    int off = 4;
    off += 2;
    wp->cur_font_id = buf[off++];
    wp->cur_size = buf[off++];

    int run_count = buf[off] | (buf[off + 1] << 8);
    off += 2;

    for (int i = 0; i < run_count && off < (int)fsize && i < WP_MAX_RUNS; i++) {
        if (off + 6 > (int)fsize) break;
        int text_len = buf[off] | (buf[off + 1] << 8);
        off += 2;
        uint8_t font_id = buf[off++];
        uint8_t size = buf[off++];
        uint8_t flags = buf[off++];
        off++;

        if (off + text_len > (int)fsize) break;

        StyledRun* r = &wp->runs[wp->run_count];
        wp_init_run(r, font_id, size, flags);
        wp_ensure_run_cap(r, text_len);
        montauk::memcpy(r->text, buf + off, text_len);
        r->len = text_len;
        off += text_len;

        wp->total_text_len += text_len;
        wp->run_count++;
    }

    if (wp->run_count == 0) {
        wp_init_run(&wp->runs[0], wp->cur_font_id, wp->cur_size, 0);
        wp->run_count = 1;
    }

    wp->cursor_run = 0;
    wp->cursor_offset = 0;
    wp->scrollbar.scroll_offset = 0;
    wp->modified = false;
    wp->wrap_dirty = true;
    wp->sel_anchor = 0;
    wp->sel_end = 0;
    wp->has_selection = false;
    wp->mouse_selecting = false;
    wp->font_dropdown_open = false;
    wp->size_dropdown_open = false;

    wp_set_filepath(wp, path);
    montauk::mfree(buf);
}
