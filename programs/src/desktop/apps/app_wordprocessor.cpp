/*
    * app_wordprocessor.cpp
    * Rich-text editor
    * Copyright (c) 2026 Daniel Hammer
*/

#include "apps_common.hpp"

// ============================================================================
// Constants
// ============================================================================

static constexpr int WP_TOOLBAR_H   = 36;
static constexpr int WP_PATHBAR_H   = 32;
static constexpr int WP_STATUS_H    = 24;
static constexpr int WP_SCROLLBAR_W = 12;
static constexpr int WP_MARGIN      = 16;
static constexpr int WP_MAX_RUNS    = 1024;
static constexpr int WP_MAX_TEXT    = 262144;  // 256KB total text
static constexpr int WP_DEFAULT_SIZE = 18;

// Font IDs
static constexpr int FONT_ROBOTO    = 0;
static constexpr int FONT_NOTOSERIF = 1;
static constexpr int FONT_C059      = 2;
static constexpr int FONT_COUNT     = 3;

// Style flags
static constexpr uint8_t STYLE_BOLD   = 0x01;
static constexpr uint8_t STYLE_ITALIC = 0x02;

// ============================================================================
// Font table — loaded on demand per word processor instance
// ============================================================================

struct WPFontTable {
    // [font_id][variant]: variant = 0=regular, 1=bold, 2=italic, 3=bolditalic
    TrueTypeFont* fonts[FONT_COUNT][4];
    bool loaded;
};

static WPFontTable wp_fonts = { {{nullptr}}, false };

static void wp_load_fonts() {
    if (wp_fonts.loaded) return;

    auto load = [](const char* path) -> TrueTypeFont* {
        TrueTypeFont* f = (TrueTypeFont*)montauk::malloc(sizeof(TrueTypeFont));
        montauk::memset(f, 0, sizeof(TrueTypeFont));
        if (!f->init(path)) {
            montauk::mfree(f);
            return nullptr;
        }
        return f;
    };

    // Roboto variants
    wp_fonts.fonts[FONT_ROBOTO][0] = fonts::system_font;  // Roboto-Medium (regular)
    wp_fonts.fonts[FONT_ROBOTO][1] = fonts::system_bold;   // Roboto-Bold
    wp_fonts.fonts[FONT_ROBOTO][2] = load("0:/fonts/Roboto-Italic.ttf");
    wp_fonts.fonts[FONT_ROBOTO][3] = load("0:/fonts/Roboto-BoldItalic.ttf");

    // NotoSerif variants
    wp_fonts.fonts[FONT_NOTOSERIF][0] = load("0:/fonts/NotoSerif-Regular.ttf");
    wp_fonts.fonts[FONT_NOTOSERIF][1] = load("0:/fonts/NotoSerif-SemiBold.ttf");
    wp_fonts.fonts[FONT_NOTOSERIF][2] = load("0:/fonts/NotoSerif-Italic.ttf");
    wp_fonts.fonts[FONT_NOTOSERIF][3] = load("0:/fonts/NotoSerif-BoldItalic.ttf");

    // C059 variants (no BoldItalic — falls back to Bold)
    wp_fonts.fonts[FONT_C059][0] = load("0:/fonts/C059-Roman.ttf");
    wp_fonts.fonts[FONT_C059][1] = load("0:/fonts/C059-Bold.ttf");
    wp_fonts.fonts[FONT_C059][2] = load("0:/fonts/C059-Italic.ttf");
    wp_fonts.fonts[FONT_C059][3] = load("0:/fonts/C059-Bold.ttf");   // fallback: bold for bolditalic

    wp_fonts.loaded = true;
}

static TrueTypeFont* wp_get_font(int font_id, uint8_t flags) {
    if (font_id < 0 || font_id >= FONT_COUNT) font_id = 0;
    int variant = 0;
    if ((flags & STYLE_BOLD) && (flags & STYLE_ITALIC)) variant = 3;
    else if (flags & STYLE_BOLD) variant = 1;
    else if (flags & STYLE_ITALIC) variant = 2;

    TrueTypeFont* f = wp_fonts.fonts[font_id][variant];
    if (f && f->valid) return f;
    // Fallback: try regular variant
    f = wp_fonts.fonts[font_id][0];
    if (f && f->valid) return f;
    // Last resort: system font
    return fonts::system_font;
}

// ============================================================================
// Document model
// ============================================================================

struct StyledRun {
    char* text;
    int len;
    int cap;
    uint8_t font_id;
    uint8_t size;
    uint8_t flags;  // STYLE_BOLD | STYLE_ITALIC
};

// Word-wrapped line info for rendering
struct WrapLine {
    int run_idx;      // which run this line starts in
    int run_offset;   // byte offset within that run
    int char_count;   // total chars in this line (across runs)
    int y;            // pixel y position in content space
    int height;       // line height in pixels
    int baseline;     // max ascent — shared baseline offset from y for all runs
};

static constexpr int WP_MAX_WRAP_LINES = 4096;

// ============================================================================
// Word Processor state
// ============================================================================

struct WordProcessorState {
    // Document
    StyledRun runs[WP_MAX_RUNS];
    int run_count;
    int total_text_len;

    // Cursor
    int cursor_run;    // which run the cursor is in
    int cursor_offset; // byte offset within that run

    // Selection (absolute positions, -1 = no selection)
    int sel_anchor;    // where selection started
    int sel_end;       // where selection extends to (cursor end)
    bool has_selection;
    bool mouse_selecting; // currently dragging

    // Current formatting for new text
    uint8_t cur_font_id;
    uint8_t cur_size;
    uint8_t cur_flags;

    // Scrollbar
    Scrollbar scrollbar;
    int content_height;

    // Word-wrap cache
    WrapLine* wrap_lines;
    int wrap_line_count;
    bool wrap_dirty;
    int last_wrap_width;  // detect resize

    // UI state
    bool modified;
    char filepath[256];
    char filename[64];
    DesktopState* desktop;

    bool show_pathbar;
    bool pathbar_save_mode;  // true = save, false = open
    char pathbar_text[256];
    int pathbar_cursor;
    int pathbar_len;

    // Dropdown state
    bool font_dropdown_open;
    bool size_dropdown_open;
};

// ============================================================================
// Run management
// ============================================================================

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

// Get absolute character position from run+offset
static int wp_abs_pos(WordProcessorState* wp, int run, int offset) {
    int pos = 0;
    for (int i = 0; i < run && i < wp->run_count; i++)
        pos += wp->runs[i].len;
    pos += offset;
    return pos;
}

// Convert absolute position to run+offset
static void wp_pos_to_run(WordProcessorState* wp, int abs_pos, int* out_run, int* out_offset) {
    if (abs_pos <= 0) { *out_run = 0; *out_offset = 0; return; }
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

// Get char at absolute position
static char wp_char_at(WordProcessorState* wp, int abs_pos) {
    int r, o;
    wp_pos_to_run(wp, abs_pos, &r, &o);
    if (r < wp->run_count && o < wp->runs[r].len)
        return wp->runs[r].text[o];
    return '\0';
}

// Get font info for char at absolute position
static void wp_font_at(WordProcessorState* wp, int abs_pos,
                        TrueTypeFont** out_font, GlyphCache** out_gc) {
    int r, o;
    wp_pos_to_run(wp, abs_pos, &r, &o);
    if (r >= wp->run_count) r = wp->run_count - 1;
    StyledRun* run = &wp->runs[r];
    *out_font = wp_get_font(run->font_id, run->flags);
    *out_gc = (*out_font && (*out_font)->valid) ? (*out_font)->get_cache(run->size) : nullptr;
}

// ============================================================================
// Text insertion / deletion
// ============================================================================

static void wp_insert_char(WordProcessorState* wp, char c) {
    if (wp->total_text_len >= WP_MAX_TEXT - 1) return;

    StyledRun* cur = &wp->runs[wp->cursor_run];

    // If cursor is at same style as current formatting, insert into current run
    if (wp_same_style(cur, wp->cur_font_id, wp->cur_size, wp->cur_flags)) {
        wp_ensure_run_cap(cur, 1);
        // Shift chars after cursor offset
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

    // Different style: need to split the run
    if (wp->cursor_offset == 0) {
        // Insert a new run before current
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
        // Append a new run after current
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
        // Split current run into three: [before][new char][after]
        if (wp->run_count + 2 > WP_MAX_RUNS) return;
        int split_at = wp->cursor_offset;
        int after_len = cur->len - split_at;

        // Create "after" run
        StyledRun after;
        wp_init_run(&after, cur->font_id, cur->size, cur->flags);
        wp_ensure_run_cap(&after, after_len);
        montauk::memcpy(after.text, cur->text + split_at, after_len);
        after.len = after_len;

        // Truncate current run to "before"
        cur->len = split_at;

        // Insert new style run and after run
        int new_idx = wp->cursor_run + 1;
        // Shift runs from new_idx onward by 2 to make room
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

static void wp_merge_adjacent(WordProcessorState* wp) {
    // Merge adjacent runs with the same style
    int i = 0;
    while (i < wp->run_count - 1) {
        StyledRun* a = &wp->runs[i];
        StyledRun* b = &wp->runs[i + 1];
        if (a->font_id == b->font_id && a->size == b->size && a->flags == b->flags) {
            int old_a_len = a->len;
            // Merge b into a
            wp_ensure_run_cap(a, b->len);
            montauk::memcpy(a->text + a->len, b->text, b->len);
            a->len += b->len;

            // Update cursor if it was in run b
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

    // Remove empty runs (except if it's the only one)
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

static void wp_backspace(WordProcessorState* wp) {
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
        if (r->len > 0) {
            r->len--;
        }
        wp->cursor_offset = r->len;
    }

    wp->total_text_len--;
    wp->modified = true;
    wp->wrap_dirty = true;
    wp_merge_adjacent(wp);
}

static void wp_delete_char(WordProcessorState* wp) {
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

// ============================================================================
// Cursor movement
// ============================================================================

static void wp_cursor_left(WordProcessorState* wp) {
    if (wp->cursor_offset > 0) {
        wp->cursor_offset--;
    } else if (wp->cursor_run > 0) {
        wp->cursor_run--;
        wp->cursor_offset = wp->runs[wp->cursor_run].len;
    }
}

static void wp_cursor_right(WordProcessorState* wp) {
    if (wp->cursor_offset < wp->runs[wp->cursor_run].len) {
        wp->cursor_offset++;
    } else if (wp->cursor_run + 1 < wp->run_count) {
        wp->cursor_run++;
        wp->cursor_offset = 0;
    }
}

// ============================================================================
// Selection helpers
// ============================================================================

static void wp_clear_selection(WordProcessorState* wp) {
    wp->has_selection = false;
    wp->sel_anchor = 0;
    wp->sel_end = 0;
}

static void wp_sel_range(WordProcessorState* wp, int* out_start, int* out_end) {
    if (wp->sel_anchor < wp->sel_end) {
        *out_start = wp->sel_anchor;
        *out_end = wp->sel_end;
    } else {
        *out_start = wp->sel_end;
        *out_end = wp->sel_anchor;
    }
}

// Start or extend selection from current cursor position
static void wp_start_selection(WordProcessorState* wp) {
    if (!wp->has_selection) {
        wp->sel_anchor = wp_abs_pos(wp, wp->cursor_run, wp->cursor_offset);
        wp->sel_end = wp->sel_anchor;
        wp->has_selection = true;
    }
}

static void wp_update_selection_to_cursor(WordProcessorState* wp) {
    wp->sel_end = wp_abs_pos(wp, wp->cursor_run, wp->cursor_offset);
    // Collapse if anchor == end
    if (wp->sel_anchor == wp->sel_end) wp->has_selection = false;
}

// Split run at absolute position, returning the run index where `abs_pos` starts.
// If abs_pos falls at a run boundary, no split needed — just returns the index.
static int wp_split_at(WordProcessorState* wp, int abs_pos) {
    int r, o;
    wp_pos_to_run(wp, abs_pos, &r, &o);
    if (o == 0) return r;
    if (o >= wp->runs[r].len) return r + 1;

    // Need to split run r at offset o
    if (wp->run_count >= WP_MAX_RUNS) return r;

    StyledRun* src = &wp->runs[r];
    int after_len = src->len - o;

    // Create the "after" portion
    StyledRun after;
    wp_init_run(&after, src->font_id, src->size, src->flags);
    wp_ensure_run_cap(&after, after_len);
    montauk::memcpy(after.text, src->text + o, after_len);
    after.len = after_len;

    // Truncate original to "before"
    src->len = o;

    // Shift runs to make room at r+1
    for (int i = wp->run_count - 1; i > r; i--)
        wp->runs[i + 1] = wp->runs[i];
    wp->runs[r + 1] = after;
    wp->run_count++;

    // Fix cursor if it was in the split run
    if (wp->cursor_run == r && wp->cursor_offset > o) {
        wp->cursor_run = r + 1;
        wp->cursor_offset -= o;
    } else if (wp->cursor_run > r) {
        wp->cursor_run++;
    }

    return r + 1;
}

// Apply a style change to the selected range.
// `mode`: 0 = set font_id, 1 = set size, 2 = toggle bold, 3 = toggle italic
static void wp_apply_style_to_selection(WordProcessorState* wp, int mode, int value) {
    if (!wp->has_selection) return;

    int sel_s, sel_e;
    wp_sel_range(wp, &sel_s, &sel_e);
    if (sel_s >= sel_e) return;

    // Split at selection boundaries to isolate affected runs
    int end_ri = wp_split_at(wp, sel_e);
    int start_ri = wp_split_at(wp, sel_s);
    // Recompute end_ri after the start split may have shifted things
    {
        int pos = 0;
        end_ri = wp->run_count;
        for (int i = 0; i < wp->run_count; i++) {
            if (pos >= sel_e) { end_ri = i; break; }
            pos += wp->runs[i].len;
        }
    }

    // Apply style to runs [start_ri, end_ri)
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

// Delete the selected text and place cursor at selection start
static void wp_delete_selection(WordProcessorState* wp) {
    if (!wp->has_selection) return;

    int sel_s, sel_e;
    wp_sel_range(wp, &sel_s, &sel_e);
    int count = sel_e - sel_s;
    if (count <= 0) { wp_clear_selection(wp); return; }

    // Place cursor at selection start
    wp_pos_to_run(wp, sel_s, &wp->cursor_run, &wp->cursor_offset);

    // Delete chars one at a time from the cursor position
    for (int i = 0; i < count; i++)
        wp_delete_char(wp);

    wp_clear_selection(wp);
}

// ============================================================================
// Word-wrap layout (absolute-position based)
// ============================================================================

// Measure the advance width of a single character at absolute position
static int wp_char_width(WordProcessorState* wp, int abs_pos) {
    TrueTypeFont* font;
    GlyphCache* gc;
    wp_font_at(wp, abs_pos, &font, &gc);
    if (!font || !gc) return 8;
    char ch = wp_char_at(wp, abs_pos);
    if (ch == '\n') return 0;
    CachedGlyph* g = font->get_glyph(gc, (unsigned char)ch);
    return g ? g->advance : 8;
}

static void wp_recompute_wrap(WordProcessorState* wp, int content_w) {
    int wrap_width = content_w - WP_MARGIN * 2 - WP_SCROLLBAR_W;
    if (wrap_width < 50) wrap_width = 50;

    // Invalidate if width changed
    if (wrap_width != wp->last_wrap_width) {
        wp->wrap_dirty = true;
        wp->last_wrap_width = wrap_width;
    }

    if (!wp->wrap_dirty && wp->wrap_lines) return;

    if (!wp->wrap_lines) {
        wp->wrap_lines = (WrapLine*)montauk::malloc(WP_MAX_WRAP_LINES * sizeof(WrapLine));
    }

    wp->wrap_line_count = 0;
    int total = wp->total_text_len;
    int y = WP_MARGIN;

    int pos = 0; // absolute char position

    while (pos < total && wp->wrap_line_count < WP_MAX_WRAP_LINES) {
        // Start a new line at position `pos`
        WrapLine* line = &wp->wrap_lines[wp->wrap_line_count];
        wp_pos_to_run(wp, pos, &line->run_idx, &line->run_offset);
        line->char_count = 0;
        line->y = y;

        // First pass: find where this line ends (newline, word-wrap, or end of text)
        int x = 0;
        int last_space = -1;   // absolute position of last space on this line
        int line_start = pos;
        int max_ascent = 0;
        int max_height = 0;

        int scan = pos;
        while (scan < total) {
            char ch = wp_char_at(wp, scan);

            // Get font metrics for this char
            TrueTypeFont* font;
            GlyphCache* gc;
            wp_font_at(wp, scan, &font, &gc);
            if (gc) {
                if (gc->ascent > max_ascent) max_ascent = gc->ascent;
                if (gc->line_height > max_height) max_height = gc->line_height;
            }

            if (ch == '\n') {
                // Include the newline in this line's char_count
                scan++;
                break;
            }

            int cw = 0;
            if (font && gc) {
                CachedGlyph* g = font->get_glyph(gc, (unsigned char)ch);
                cw = g ? g->advance : 8;
            } else {
                cw = 8;
            }

            if (x + cw > wrap_width && scan > line_start) {
                // Need to wrap. Try breaking at last space.
                if (last_space >= line_start) {
                    scan = last_space + 1; // wrap after the space
                }
                // else: no space found, break right here (hard break)
                break;
            }

            if (ch == ' ') last_space = scan;
            x += cw;
            scan++;
        }

        line->char_count = scan - line_start;

        // If we didn't encounter any chars (empty doc), set defaults
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

        // Recompute max_ascent/max_height precisely by walking the line's runs
        // (the scan above may have overshot for word-wrap — recalculate for actual chars)
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
                if (ro >= r->len) { ri++; ro = 0; }
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

        // If we consumed nothing (shouldn't happen), advance to avoid infinite loop
        if (line->char_count == 0 && pos < total) {
            pos++;
        }
    }

    // Add trailing line if document ends with newline (cursor needs somewhere to go)
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

    // Ensure at least one line exists
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

// ============================================================================
// Cursor position helpers (using wrap lines)
// ============================================================================

static int wp_find_wrap_line(WordProcessorState* wp, int abs_pos) {
    int pos = 0;
    for (int i = 0; i < wp->wrap_line_count; i++) {
        int next_pos = pos + wp->wrap_lines[i].char_count;
        if (abs_pos < next_pos || i == wp->wrap_line_count - 1)
            return i;
        pos = next_pos;
    }
    return 0;
}

static int wp_wrap_line_start(WordProcessorState* wp, int line_idx) {
    int pos = 0;
    for (int i = 0; i < line_idx && i < wp->wrap_line_count; i++)
        pos += wp->wrap_lines[i].char_count;
    return pos;
}

static void wp_cursor_up(WordProcessorState* wp) {
    int abs = wp_abs_pos(wp, wp->cursor_run, wp->cursor_offset);
    int line = wp_find_wrap_line(wp, abs);
    if (line <= 0) return;

    int col = abs - wp_wrap_line_start(wp, line);
    int prev_start = wp_wrap_line_start(wp, line - 1);
    int prev_len = wp->wrap_lines[line - 1].char_count;
    int new_col = col < prev_len ? col : prev_len;
    // Don't land on the newline at end of line
    if (new_col > 0 && new_col == prev_len) {
        char ch = wp_char_at(wp, prev_start + prev_len - 1);
        if (ch == '\n') new_col = prev_len - 1;
    }
    wp_pos_to_run(wp, prev_start + new_col, &wp->cursor_run, &wp->cursor_offset);
}

static void wp_cursor_down(WordProcessorState* wp) {
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

static void wp_ensure_cursor_visible(WordProcessorState* wp, int view_h) {
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

    // Clamp scroll
    int ms = wp->scrollbar.max_scroll();
    if (wp->scrollbar.scroll_offset > ms) wp->scrollbar.scroll_offset = ms;
    if (wp->scrollbar.scroll_offset < 0) wp->scrollbar.scroll_offset = 0;
}

// ============================================================================
// File I/O - MWP binary format
// ============================================================================

static void wp_set_filepath(WordProcessorState* wp, const char* path) {
    montauk::strncpy(wp->filepath, path, 255);
    int last_slash = -1;
    for (int i = 0; path[i]; i++)
        if (path[i] == '/') last_slash = i;
    if (last_slash >= 0)
        montauk::strncpy(wp->filename, path + last_slash + 1, 63);
    else
        montauk::strncpy(wp->filename, path, 63);
}

static void wp_open_save_pathbar(WordProcessorState* wp) {
    wp->show_pathbar = true;
    wp->pathbar_save_mode = true;
    montauk::strncpy(wp->pathbar_text, wp->filepath, 255);
    wp->pathbar_len = montauk::slen(wp->pathbar_text);
    wp->pathbar_cursor = wp->pathbar_len;
}

static void wp_save_file(WordProcessorState* wp) {
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

static void wp_load_file(WordProcessorState* wp, const char* path) {
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

    for (int i = 0; i < wp->run_count; i++)
        wp_free_run(&wp->runs[i]);

    int off = 4;
    off += 2;
    wp->cur_font_id = buf[off++];
    wp->cur_size = buf[off++];

    int run_count = buf[off] | (buf[off + 1] << 8);
    off += 2;

    wp->run_count = 0;
    wp->total_text_len = 0;

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

    montauk::strncpy(wp->filepath, path, 255);

    int last_slash = -1;
    for (int i = 0; path[i]; i++)
        if (path[i] == '/') last_slash = i;
    if (last_slash >= 0)
        montauk::strncpy(wp->filename, path + last_slash + 1, 63);
    else
        montauk::strncpy(wp->filename, path, 63);

    montauk::mfree(buf);
}

// ============================================================================
// Drawing
// ============================================================================

static const char* font_names[] = { "Roboto", "NotoSerif", "C059" };
static const int size_options[] = { 12, 14, 16, 18, 20, 24, 28, 36 };
static constexpr int SIZE_OPTION_COUNT = 8;

static void wp_on_draw(Window* win, Framebuffer& fb) {
    WordProcessorState* wp = (WordProcessorState*)win->app_data;
    if (!wp) return;

    Canvas c(win);
    c.fill(colors::WINDOW_BG);

    int sfh = system_font_height();
    Color toolbar_bg = Color::from_rgb(0xF5, 0xF5, 0xF5);
    Color btn_bg = Color::from_rgb(0xE8, 0xE8, 0xE8);
    Color btn_active = Color::from_rgb(0xC0, 0xD0, 0xE8);

    // ---- Toolbar (36px) ----
    c.fill_rect(0, 0, c.w, WP_TOOLBAR_H, toolbar_bg);

    // Open button
    c.fill_rounded_rect(4, 6, 24, 24, 3, btn_bg);
    if (wp->desktop && wp->desktop->icon_folder.pixels)
        c.icon(8, 10, wp->desktop->icon_folder);

    // Save button
    c.fill_rounded_rect(32, 6, 24, 24, 3, btn_bg);
    if (wp->desktop && wp->desktop->icon_save.pixels)
        c.icon(36, 10, wp->desktop->icon_save);

    c.vline(60, 4, 28, colors::BORDER);

    // Bold button
    Color bold_bg = (wp->cur_flags & STYLE_BOLD) ? btn_active : btn_bg;
    c.fill_rounded_rect(66, 6, 24, 24, 3, bold_bg);
    if (fonts::system_bold && fonts::system_bold->valid) {
        fonts::system_bold->draw_to_buffer(c.pixels, c.w, c.h, 73, 8, "B", colors::TEXT_COLOR, fonts::UI_SIZE);
    } else {
        c.text(73, 10, "B", colors::TEXT_COLOR);
    }

    // Italic button
    Color italic_bg = (wp->cur_flags & STYLE_ITALIC) ? btn_active : btn_bg;
    c.fill_rounded_rect(94, 6, 24, 24, 3, italic_bg);
    {
        TrueTypeFont* italic_font = wp_get_font(FONT_ROBOTO, STYLE_ITALIC);
        if (italic_font && italic_font->valid) {
            italic_font->draw_to_buffer(c.pixels, c.w, c.h, 103, 8, "I", colors::TEXT_COLOR, fonts::UI_SIZE);
        } else {
            c.text(103, 10, "I", colors::TEXT_COLOR);
        }
    }

    c.vline(122, 4, 28, colors::BORDER);

    // Font dropdown
    c.fill_rounded_rect(128, 6, 90, 24, 3, btn_bg);
    {
        const char* fn = font_names[wp->cur_font_id < FONT_COUNT ? wp->cur_font_id : 0];
        c.text(134, (WP_TOOLBAR_H - sfh) / 2, fn, colors::TEXT_COLOR);
    }

    // Size dropdown
    c.fill_rounded_rect(224, 6, 44, 24, 3, btn_bg);
    {
        char sz[8];
        snprintf(sz, 8, "%d", (int)wp->cur_size);
        c.text(232, (WP_TOOLBAR_H - sfh) / 2, sz, colors::TEXT_COLOR);
    }

    c.vline(272, 4, 28, colors::BORDER);

    // Section sign button
    c.fill_rounded_rect(278, 6, 24, 24, 3, btn_bg);
    {
        char section[2] = { (char)0xA7, '\0' };
        TrueTypeFont* sf = wp_get_font(FONT_ROBOTO, 0);
        if (sf && sf->valid)
            sf->draw_to_buffer(c.pixels, c.w, c.h, 284, 8, section, colors::TEXT_COLOR, fonts::UI_SIZE);
        else
            c.text(284, 10, "S", colors::TEXT_COLOR);
    }

    // Filename + modified
    {
        char label[128];
        if (wp->filename[0])
            snprintf(label, 128, "%s%s", wp->filename, wp->modified ? " *" : "");
        else
            snprintf(label, 128, "Untitled%s", wp->modified ? " *" : "");
        c.text(312, (WP_TOOLBAR_H - sfh) / 2, label, Color::from_rgb(0x88, 0x88, 0x88));
    }

    c.hline(0, WP_TOOLBAR_H - 1, c.w, colors::BORDER);

    // ---- Path bar (conditional) ----
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
        c.text(inp_x + 4, text_y, wp->pathbar_text, colors::TEXT_COLOR);

        char prefix[256];
        int plen = wp->pathbar_cursor;
        if (plen > 255) plen = 255;
        for (int i = 0; i < plen; i++) prefix[i] = wp->pathbar_text[i];
        prefix[plen] = '\0';
        int cx = inp_x + 4 + text_width(prefix);
        c.fill_rect(cx, inp_y + 3, 2, inp_h - 6, colors::ACCENT);

        int ob_x = inp_x + inp_w + 6;
        const char* pb_label = wp->pathbar_save_mode ? "Save" : "Open";
        c.button(ob_x, inp_y, btn_w, inp_h, pb_label, colors::ACCENT, colors::WHITE, 3);

        c.hline(0, pb_y + WP_PATHBAR_H - 1, c.w, colors::BORDER);
        edit_y = WP_TOOLBAR_H + WP_PATHBAR_H;
    }

    // ---- Text area ----
    int text_area_h = c.h - edit_y - WP_STATUS_H;
    int text_area_w = c.w;

    wp_recompute_wrap(wp, text_area_w);

    // Update scrollbar
    wp->scrollbar.bounds = {c.w - WP_SCROLLBAR_W, edit_y, WP_SCROLLBAR_W, text_area_h};
    wp->scrollbar.content_height = wp->content_height;
    wp->scrollbar.view_height = text_area_h;

    // Clamp scroll offset
    {
        int ms = wp->scrollbar.max_scroll();
        if (wp->scrollbar.scroll_offset > ms) wp->scrollbar.scroll_offset = ms;
        if (wp->scrollbar.scroll_offset < 0) wp->scrollbar.scroll_offset = 0;
    }

    wp_ensure_cursor_visible(wp, text_area_h);

    int scroll_y = wp->scrollbar.scroll_offset;

    // Draw text content
    int cursor_abs = wp_abs_pos(wp, wp->cursor_run, wp->cursor_offset);
    int sel_s = 0, sel_e = 0;
    if (wp->has_selection) wp_sel_range(wp, &sel_s, &sel_e);
    Color sel_bg = Color::from_rgb(0xB0, 0xD0, 0xF0);

    for (int li = 0; li < wp->wrap_line_count; li++) {
        WrapLine* wl = &wp->wrap_lines[li];
        int py = edit_y + wl->y - scroll_y;

        // Clip
        if (py + wl->height <= edit_y) continue;
        if (py >= edit_y + text_area_h) break;

        // Walk through runs to render this line's chars
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
                ri++; ro = 0;
                continue;
            }

            GlyphCache* gc = font->get_cache(r->size);
            int baseline = py + wl->baseline;

            int avail = r->len - ro;
            int to_draw = avail < chars_left ? avail : chars_left;

            for (int ci = 0; ci < to_draw; ci++) {
                char ch = r->text[ro + ci];
                int abs_ch = line_abs_start + char_idx;

                // Compute char advance for selection highlight
                int char_adv = 0;
                if (ch != '\n' && (ch >= 32 || ch < 0)) {
                    CachedGlyph* g = font->get_glyph(gc, (unsigned char)ch);
                    char_adv = g ? g->advance : 8;
                }

                // Draw selection highlight behind text
                if (wp->has_selection && abs_ch >= sel_s && abs_ch < sel_e) {
                    int sel_w = char_adv > 0 ? char_adv : 6;
                    c.fill_rect(x, py, sel_w, wl->height, sel_bg);
                }

                // Draw cursor
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
                            c.pixels, c.w, c.h, x, baseline, (unsigned char)ch,
                            text_col, gc);
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

        // Draw cursor at end of line only on the last wrap line
        if (line_abs_start + char_idx == cursor_abs && li == wp->wrap_line_count - 1) {
            int cur_h = wl->height;
            if (py >= edit_y && py + cur_h <= edit_y + text_area_h)
                c.fill_rect(x, py, 2, cur_h, colors::ACCENT);
        }
    }

    // ---- Scrollbar ----
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

    // ---- Status bar ----
    int status_y = c.h - WP_STATUS_H;
    c.fill_rect(0, status_y, c.w, WP_STATUS_H, Color::from_rgb(0x2B, 0x3E, 0x50));
    int status_text_y = status_y + (WP_STATUS_H - sfh) / 2;

    char status_left[128];
    snprintf(status_left, 128, " %s %dpt  %s%s",
        font_names[wp->cur_font_id < FONT_COUNT ? wp->cur_font_id : 0], (int)wp->cur_size,
        (wp->cur_flags & STYLE_BOLD) ? "Bold " : "",
        (wp->cur_flags & STYLE_ITALIC) ? "Italic" : "");
    c.text(4, status_text_y, status_left, colors::PANEL_TEXT);

    char status_right[32];
    snprintf(status_right, 32, "%d chars ", wp->total_text_len);
    int sr_w = text_width(status_right);
    c.text(c.w - sr_w - 4, status_text_y, status_right, colors::PANEL_TEXT);

    // ---- Dropdown overlays ----
    if (wp->font_dropdown_open) {
        int dx = 128, dy = WP_TOOLBAR_H;
        int dw = 110, dh = FONT_COUNT * 26 + 4;
        c.fill_rect(dx, dy, dw, dh, colors::MENU_BG);
        c.rect(dx, dy, dw, dh, colors::BORDER);
        for (int i = 0; i < FONT_COUNT; i++) {
            int iy = dy + 2 + i * 26;
            if (i == wp->cur_font_id)
                c.fill_rect(dx + 2, iy, dw - 4, 24, colors::MENU_HOVER);
            c.text(dx + 8, iy + (24 - sfh) / 2, font_names[i], colors::TEXT_COLOR);
        }
    }

    if (wp->size_dropdown_open) {
        int dx = 224, dy = WP_TOOLBAR_H;
        int dw = 56, dh = SIZE_OPTION_COUNT * 26 + 4;
        c.fill_rect(dx, dy, dw, dh, colors::MENU_BG);
        c.rect(dx, dy, dw, dh, colors::BORDER);
        for (int i = 0; i < SIZE_OPTION_COUNT; i++) {
            int iy = dy + 2 + i * 26;
            if (size_options[i] == wp->cur_size)
                c.fill_rect(dx + 2, iy, dw - 4, 24, colors::MENU_HOVER);
            char sz[8];
            snprintf(sz, 8, "%d", size_options[i]);
            c.text(dx + 8, iy + (24 - sfh) / 2, sz, colors::TEXT_COLOR);
        }
    }
}

// ============================================================================
// Mouse handling
// ============================================================================

static void wp_on_mouse(Window* win, MouseEvent& ev) {
    WordProcessorState* wp = (WordProcessorState*)win->app_data;
    if (!wp) return;

    Rect cr = win->content_rect();
    int local_x = ev.x - cr.x;
    int local_y = ev.y - cr.y;
    int edit_y = WP_TOOLBAR_H + (wp->show_pathbar ? WP_PATHBAR_H : 0);
    int text_area_h = cr.h - edit_y - WP_STATUS_H;

    // Scrollbar
    MouseEvent local_ev = ev;
    local_ev.x = local_x;
    local_ev.y = local_y;
    wp->scrollbar.handle_mouse(local_ev);

    // ---- Font dropdown clicks ----
    if (wp->font_dropdown_open && ev.left_pressed()) {
        int dx = 128, dy = WP_TOOLBAR_H;
        int dh = FONT_COUNT * 26 + 4;
        if (local_x >= dx && local_x < dx + 110 && local_y >= dy && local_y < dy + dh) {
            int idx = (local_y - dy - 2) / 26;
            if (idx >= 0 && idx < FONT_COUNT) {
                if (wp->has_selection) wp_apply_style_to_selection(wp, 0, idx);
                wp->cur_font_id = (uint8_t)idx;
            }
        }
        wp->font_dropdown_open = false;
        return;
    }

    // ---- Size dropdown clicks ----
    if (wp->size_dropdown_open && ev.left_pressed()) {
        int dx = 224, dy = WP_TOOLBAR_H;
        int dh = SIZE_OPTION_COUNT * 26 + 4;
        if (local_x >= dx && local_x < dx + 56 && local_y >= dy && local_y < dy + dh) {
            int idx = (local_y - dy - 2) / 26;
            if (idx >= 0 && idx < SIZE_OPTION_COUNT) {
                if (wp->has_selection) wp_apply_style_to_selection(wp, 1, size_options[idx]);
                wp->cur_size = (uint8_t)size_options[idx];
                wp->wrap_dirty = true;
            }
        }
        wp->size_dropdown_open = false;
        return;
    }

    // ---- Toolbar clicks ----
    if (ev.left_pressed() && local_y < WP_TOOLBAR_H) {
        if (local_x >= 4 && local_x < 28 && local_y >= 6 && local_y < 30) {
            wp->show_pathbar = !wp->show_pathbar;
            wp->pathbar_save_mode = false;
            if (wp->show_pathbar) {
                montauk::strncpy(wp->pathbar_text, wp->filepath, 255);
                wp->pathbar_len = montauk::slen(wp->pathbar_text);
                wp->pathbar_cursor = wp->pathbar_len;
            }
            return;
        }
        if (local_x >= 32 && local_x < 56 && local_y >= 6 && local_y < 30) {
            wp_save_file(wp);
            return;
        }
        if (local_x >= 66 && local_x < 90 && local_y >= 6 && local_y < 30) {
            if (wp->has_selection) wp_apply_style_to_selection(wp, 2, 0);
            wp->cur_flags ^= STYLE_BOLD;
            return;
        }
        if (local_x >= 94 && local_x < 118 && local_y >= 6 && local_y < 30) {
            if (wp->has_selection) wp_apply_style_to_selection(wp, 3, 0);
            wp->cur_flags ^= STYLE_ITALIC;
            return;
        }
        if (local_x >= 128 && local_x < 218 && local_y >= 6 && local_y < 30) {
            wp->font_dropdown_open = !wp->font_dropdown_open;
            wp->size_dropdown_open = false;
            return;
        }
        if (local_x >= 224 && local_x < 268 && local_y >= 6 && local_y < 30) {
            wp->size_dropdown_open = !wp->size_dropdown_open;
            wp->font_dropdown_open = false;
            return;
        }
        if (local_x >= 278 && local_x < 302 && local_y >= 6 && local_y < 30) {
            wp_insert_char(wp, (char)0xA7);
            return;
        }
        return;
    }

    // ---- Path bar clicks ----
    if (wp->show_pathbar && local_y >= WP_TOOLBAR_H && local_y < WP_TOOLBAR_H + WP_PATHBAR_H) {
        if (ev.left_pressed()) {
            int btn_w = 56;
            int inp_w = cr.w - 8 - btn_w - 12;
            int ob_x = 8 + inp_w + 6;
            if (local_x >= ob_x && local_x < ob_x + btn_w) {
                if (wp->pathbar_text[0]) {
                    if (wp->pathbar_save_mode) {
                        wp_set_filepath(wp, wp->pathbar_text);
                        wp_save_file(wp);
                    } else {
                        wp_load_file(wp, wp->pathbar_text);
                    }
                    char title[64];
                    snprintf(title, 64, "%s - Word Processor", wp->filename);
                    montauk::strncpy(win->title, title, 63);
                    wp->show_pathbar = false;
                }
            }
        }
        return;
    }

    // ---- Text area: hit-test helper ----
    auto hit_test = [&](int lx, int ly) -> int {
        int click_y = ly - edit_y + wp->scrollbar.scroll_offset;
        int click_x = lx - WP_MARGIN;

        int target_line = wp->wrap_line_count - 1;
        for (int i = 0; i < wp->wrap_line_count; i++) {
            if (click_y >= wp->wrap_lines[i].y &&
                click_y < wp->wrap_lines[i].y + wp->wrap_lines[i].height) {
                target_line = i;
                break;
            }
        }

        WrapLine* wl = &wp->wrap_lines[target_line];
        int ri = wl->run_idx;
        int ro = wl->run_offset;
        int chars_left = wl->char_count;
        int x = 0;
        int best_abs = wp_wrap_line_start(wp, target_line);

        while (chars_left > 0 && ri < wp->run_count) {
            StyledRun* r = &wp->runs[ri];
            TrueTypeFont* font = wp_get_font(r->font_id, r->flags);
            if (!font || !font->valid) { ri++; ro = 0; continue; }

            GlyphCache* gc = font->get_cache(r->size);
            int avail = r->len - ro;
            int to_check = avail < chars_left ? avail : chars_left;

            for (int ci = 0; ci < to_check; ci++) {
                char ch = r->text[ro + ci];
                CachedGlyph* g = (ch >= 32 || ch < 0) && ch != '\n'
                    ? font->get_glyph(gc, (unsigned char)ch) : nullptr;
                int char_w = g ? g->advance : 0;

                if (x + char_w / 2 > click_x) return best_abs;
                x += char_w;
                best_abs++;
            }

            chars_left -= to_check;
            ro += to_check;
            if (ro >= r->len) { ri++; ro = 0; }
        }
        return best_abs;
    };

    // ---- Text area: mouse press - start selection or place cursor ----
    if (ev.left_pressed() && local_y >= edit_y && local_y < edit_y + text_area_h) {
        wp->font_dropdown_open = false;
        wp->size_dropdown_open = false;

        int abs = hit_test(local_x, local_y);
        wp_pos_to_run(wp, abs, &wp->cursor_run, &wp->cursor_offset);
        wp->sel_anchor = abs;
        wp->sel_end = abs;
        wp->has_selection = false;
        wp->mouse_selecting = true;
        return;
    }

    // ---- Text area: mouse drag - extend selection ----
    if (wp->mouse_selecting && ev.left_held() && local_y >= edit_y - 20) {
        int abs = hit_test(local_x, local_y);
        wp->sel_end = abs;
        wp_pos_to_run(wp, abs, &wp->cursor_run, &wp->cursor_offset);
        wp->has_selection = (wp->sel_anchor != wp->sel_end);
        return;
    }

    // ---- Mouse release: end selection ----
    if (wp->mouse_selecting && ev.left_released()) {
        wp->mouse_selecting = false;
        return;
    }

    // ---- Scroll ----
    if (ev.scroll != 0 && local_y >= edit_y && local_y < edit_y + text_area_h) {
        wp->scrollbar.scroll_offset -= ev.scroll * 40;
        int ms = wp->scrollbar.max_scroll();
        if (wp->scrollbar.scroll_offset < 0) wp->scrollbar.scroll_offset = 0;
        if (wp->scrollbar.scroll_offset > ms) wp->scrollbar.scroll_offset = ms;
    }
}

// ============================================================================
// Keyboard handling
// ============================================================================

static void wp_on_key(Window* win, const Montauk::KeyEvent& key) {
    WordProcessorState* wp = (WordProcessorState*)win->app_data;
    if (!wp || !key.pressed) return;

    // ---- Path bar mode ----
    if (wp->show_pathbar) {
        if (key.ascii == '\n' || key.ascii == '\r') {
            if (wp->pathbar_text[0]) {
                if (wp->pathbar_save_mode) {
                    wp_set_filepath(wp, wp->pathbar_text);
                    wp_save_file(wp);
                } else {
                    wp_load_file(wp, wp->pathbar_text);
                }
                char title[64];
                snprintf(title, 64, "%s - Word Processor", wp->filename);
                montauk::strncpy(win->title, title, 63);
                wp->show_pathbar = false;
            }
            return;
        }
        if (key.scancode == 0x01) { wp->show_pathbar = false; return; }
        if (key.ascii == '\b' || key.scancode == 0x0E) {
            if (wp->pathbar_cursor > 0) {
                for (int i = wp->pathbar_cursor - 1; i < wp->pathbar_len - 1; i++)
                    wp->pathbar_text[i] = wp->pathbar_text[i + 1];
                wp->pathbar_len--;
                wp->pathbar_cursor--;
                wp->pathbar_text[wp->pathbar_len] = '\0';
            }
            return;
        }
        if (key.scancode == 0x4B) { if (wp->pathbar_cursor > 0) wp->pathbar_cursor--; return; }
        if (key.scancode == 0x4D) { if (wp->pathbar_cursor < wp->pathbar_len) wp->pathbar_cursor++; return; }
        if (key.ascii >= 32 && key.ascii < 127 && wp->pathbar_len < 254) {
            for (int i = wp->pathbar_len; i > wp->pathbar_cursor; i--)
                wp->pathbar_text[i] = wp->pathbar_text[i - 1];
            wp->pathbar_text[wp->pathbar_cursor] = key.ascii;
            wp->pathbar_cursor++;
            wp->pathbar_len++;
            wp->pathbar_text[wp->pathbar_len] = '\0';
            return;
        }
        return;
    }

    // Close dropdowns on any key
    wp->font_dropdown_open = false;
    wp->size_dropdown_open = false;

    // Ctrl+S
    if (key.ctrl && (key.ascii == 's' || key.ascii == 'S')) {
        wp_save_file(wp);
        return;
    }
    // Ctrl+O
    if (key.ctrl && (key.ascii == 'o' || key.ascii == 'O')) {
        wp->show_pathbar = !wp->show_pathbar;
        wp->pathbar_save_mode = false;
        if (wp->show_pathbar) {
            montauk::strncpy(wp->pathbar_text, wp->filepath, 255);
            wp->pathbar_len = montauk::slen(wp->pathbar_text);
            wp->pathbar_cursor = wp->pathbar_len;
        }
        return;
    }
    // Ctrl+B
    if (key.ctrl && (key.ascii == 'b' || key.ascii == 'B')) {
        if (wp->has_selection) wp_apply_style_to_selection(wp, 2, 0);
        wp->cur_flags ^= STYLE_BOLD;
        return;
    }
    // Ctrl+I
    if (key.ctrl && (key.ascii == 'i' || key.ascii == 'I')) {
        if (wp->has_selection) wp_apply_style_to_selection(wp, 3, 0);
        wp->cur_flags ^= STYLE_ITALIC;
        return;
    }

    // Arrow keys (Shift extends selection)
    if (key.scancode == 0x48) { // Up
        if (key.shift) wp_start_selection(wp);
        else wp_clear_selection(wp);
        wp_cursor_up(wp);
        if (key.shift) wp_update_selection_to_cursor(wp);
        return;
    }
    if (key.scancode == 0x50) { // Down
        if (key.shift) wp_start_selection(wp);
        else wp_clear_selection(wp);
        wp_cursor_down(wp);
        if (key.shift) wp_update_selection_to_cursor(wp);
        return;
    }
    if (key.scancode == 0x4B) { // Left
        if (key.shift) wp_start_selection(wp);
        else if (wp->has_selection) {
            int s, e; wp_sel_range(wp, &s, &e);
            wp_pos_to_run(wp, s, &wp->cursor_run, &wp->cursor_offset);
            wp_clear_selection(wp);
            return;
        }
        wp_cursor_left(wp);
        if (key.shift) wp_update_selection_to_cursor(wp);
        return;
    }
    if (key.scancode == 0x4D) { // Right
        if (key.shift) wp_start_selection(wp);
        else if (wp->has_selection) {
            int s, e; wp_sel_range(wp, &s, &e);
            wp_pos_to_run(wp, e, &wp->cursor_run, &wp->cursor_offset);
            wp_clear_selection(wp);
            return;
        }
        wp_cursor_right(wp);
        if (key.shift) wp_update_selection_to_cursor(wp);
        return;
    }

    // Home
    if (key.scancode == 0x47) {
        if (key.shift) wp_start_selection(wp);
        else wp_clear_selection(wp);
        int abs = wp_abs_pos(wp, wp->cursor_run, wp->cursor_offset);
        int line = wp_find_wrap_line(wp, abs);
        int start = wp_wrap_line_start(wp, line);
        wp_pos_to_run(wp, start, &wp->cursor_run, &wp->cursor_offset);
        if (key.shift) wp_update_selection_to_cursor(wp);
        return;
    }
    // End
    if (key.scancode == 0x4F) {
        if (key.shift) wp_start_selection(wp);
        else wp_clear_selection(wp);
        int abs = wp_abs_pos(wp, wp->cursor_run, wp->cursor_offset);
        int line = wp_find_wrap_line(wp, abs);
        int start = wp_wrap_line_start(wp, line);
        int end = start + wp->wrap_lines[line].char_count;
        if (end > start) {
            char ch = wp_char_at(wp, end - 1);
            if (ch == '\n') end--;
        }
        wp_pos_to_run(wp, end, &wp->cursor_run, &wp->cursor_offset);
        if (key.shift) wp_update_selection_to_cursor(wp);
        return;
    }

    // Ctrl+A - select all
    if (key.ctrl && (key.ascii == 'a' || key.ascii == 'A')) {
        wp->sel_anchor = 0;
        wp->sel_end = wp->total_text_len;
        wp->has_selection = (wp->total_text_len > 0);
        wp_pos_to_run(wp, wp->total_text_len, &wp->cursor_run, &wp->cursor_offset);
        return;
    }

    // Delete
    if (key.scancode == 0x53) {
        if (wp->has_selection) wp_delete_selection(wp);
        else wp_delete_char(wp);
        return;
    }

    // Backspace
    if (key.ascii == '\b' || key.scancode == 0x0E) {
        if (wp->has_selection) wp_delete_selection(wp);
        else wp_backspace(wp);
        return;
    }

    // Enter
    if (key.ascii == '\n' || key.ascii == '\r') {
        if (wp->has_selection) wp_delete_selection(wp);
        wp_insert_char(wp, '\n');
        return;
    }

    // Tab
    if (key.ascii == '\t') {
        if (wp->has_selection) wp_delete_selection(wp);
        for (int i = 0; i < 4; i++) wp_insert_char(wp, ' ');
        return;
    }

    // Printable characters
    if (key.ascii >= 32 && key.ascii < 127) {
        if (wp->has_selection) wp_delete_selection(wp);
        wp_insert_char(wp, key.ascii);
        return;
    }
}

// ============================================================================
// Close handler
// ============================================================================

static void wp_on_close(Window* win) {
    WordProcessorState* wp = (WordProcessorState*)win->app_data;
    if (wp) {
        for (int i = 0; i < wp->run_count; i++)
            wp_free_run(&wp->runs[i]);
        if (wp->wrap_lines) montauk::mfree(wp->wrap_lines);
        montauk::mfree(wp);
        win->app_data = nullptr;
    }
}

// ============================================================================
// Word Processor launcher
// ============================================================================

void open_wordprocessor(DesktopState* ds) {
    wp_load_fonts();

    int idx = desktop_create_window(ds, "Word Processor", 140, 50, 640, 480);
    if (idx < 0) return;

    Window* win = &ds->windows[idx];
    WordProcessorState* wp = (WordProcessorState*)montauk::malloc(sizeof(WordProcessorState));
    montauk::memset(wp, 0, sizeof(WordProcessorState));

    wp_init_run(&wp->runs[0], FONT_ROBOTO, WP_DEFAULT_SIZE, 0);
    wp->run_count = 1;
    wp->total_text_len = 0;
    wp->cursor_run = 0;
    wp->cursor_offset = 0;

    wp->cur_font_id = FONT_ROBOTO;
    wp->cur_size = WP_DEFAULT_SIZE;
    wp->cur_flags = 0;

    wp->scrollbar.init(0, 0, WP_SCROLLBAR_W, 100);

    wp->modified = false;
    wp->desktop = ds;
    wp->show_pathbar = false;
    wp->wrap_dirty = true;
    wp->wrap_lines = nullptr;
    wp->last_wrap_width = 0;
    wp->font_dropdown_open = false;
    wp->size_dropdown_open = false;

    win->app_data = wp;
    win->on_draw = wp_on_draw;
    win->on_mouse = wp_on_mouse;
    win->on_key = wp_on_key;
    win->on_close = wp_on_close;
}
