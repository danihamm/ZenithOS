/*
    * app_klog.cpp
    * MontaukOS Desktop - Kernel Log viewer (tails the kernel ring buffer)
    * Copyright (c) 2026 Daniel Hammer
*/

#include "apps_common.hpp"

// ============================================================================
// Kernel Log state
// ============================================================================

static constexpr int KLOG_READ_SIZE = 65536; // matches kernel ring buffer size
static constexpr int KLOG_POLL_MS   = 250;

struct KlogState {
    TerminalState term;
    char* klog_buf;
    int last_len;
    char last_tail_byte;
    uint64_t last_poll_ms;
};

// ============================================================================
// Re-feed log into the terminal (fills scrollback + visible screen)
// ============================================================================

static void klog_refeed(KlogState* klog, int n) {
    // Find enough lines to fill scrollback + visible screen
    int lines_needed = klog->term.rows + klog->term.max_scrollback;
    int start = 0;
    int line_count = 0;
    for (int i = n - 1; i >= 0; i--) {
        if (klog->klog_buf[i] == '\n') {
            line_count++;
            if (line_count >= lines_needed) { start = i + 1; break; }
        }
    }

    // Reset terminal state
    klog->term.scrollback_lines = 0;
    klog->term.view_offset = 0;
    klog->term.cursor_x = 0;
    klog->term.cursor_y = 0;
    klog->term.current_fg = colors::TERM_FG;
    klog->term.current_bg = colors::TERM_BG;

    // Clear visible screen
    TermCell* screen = term_screen_row(&klog->term, 0);
    int total = klog->term.cols * klog->term.rows;
    for (int i = 0; i < total; i++)
        screen[i] = {' ', colors::TERM_FG, colors::TERM_BG};

    terminal_feed(&klog->term, klog->klog_buf + start, n - start);
}

// ============================================================================
// Callbacks
// ============================================================================

static void klog_on_draw(Window* win, Framebuffer& fb) {
    KlogState* klog = (KlogState*)win->app_data;
    if (!klog) return;

    Rect cr = win->content_rect();

    // Handle resize
    int new_cols = cr.w / mono_cell_width();
    int new_rows = cr.h / mono_cell_height();
    if (new_cols != klog->term.cols || new_rows != klog->term.rows) {
        terminal_resize(&klog->term, new_cols, new_rows);
        // Refeed to fill the new grid with log content
        if (klog->last_len > 0) {
            klog_refeed(klog, klog->last_len);
        }
    }

    bool was_dirty = klog->term.dirty;
    terminal_render(&klog->term, win->content, cr.w, cr.h);

    // Draw scrollbar overlay when scrollback exists
    if (was_dirty && klog->term.scrollback_lines > 0) {
        int cell_h = mono_cell_height();
        int total_rows = klog->term.scrollback_lines + klog->term.rows;
        int total_h = total_rows * cell_h;
        int view_h = cr.h;
        if (total_h > view_h) {
            Canvas c(win->content, cr.w, cr.h);
            int sb_w = 4;
            int sb_x = cr.w - sb_w - 2;
            int thumb_h = (view_h * view_h) / total_h;
            if (thumb_h < 20) thumb_h = 20;
            int scroll_from_top = klog->term.scrollback_lines - klog->term.view_offset;
            int max_scroll = klog->term.scrollback_lines;
            int thumb_y = (max_scroll > 0)
                ? (scroll_from_top * (view_h - thumb_h)) / max_scroll : 0;
            c.fill_rect(sb_x, 0, sb_w, view_h, Color::from_rgb(0x33, 0x33, 0x33));
            c.fill_rect(sb_x, thumb_y, sb_w, thumb_h, Color::from_rgb(0x88, 0x88, 0x88));
        }
    }
}

static void klog_on_mouse(Window* win, MouseEvent& ev) {
    KlogState* klog = (KlogState*)win->app_data;
    if (!klog) return;

    if (ev.scroll != 0) {
        klog->term.view_offset += (ev.scroll < 0) ? 3 : -3;
        if (klog->term.view_offset < 0) klog->term.view_offset = 0;
        if (klog->term.view_offset > klog->term.scrollback_lines)
            klog->term.view_offset = klog->term.scrollback_lines;
        klog->term.dirty = true;
    }
}

static void klog_on_key(Window* win, const Montauk::KeyEvent& key) {
    // Read-only viewer — no keyboard input
}

static void klog_on_poll(Window* win) {
    KlogState* klog = (KlogState*)win->app_data;
    if (!klog) return;

    uint64_t now = montauk::get_milliseconds();
    if (now - klog->last_poll_ms < KLOG_POLL_MS) return;
    klog->last_poll_ms = now;

    int n = (int)montauk::read_klog(klog->klog_buf, KLOG_READ_SIZE);
    if (n <= 0 && klog->last_len <= 0) return;

    if (n > klog->last_len) {
        // Buffer grew — feed only the new portion
        terminal_feed(&klog->term, klog->klog_buf + klog->last_len, n - klog->last_len);
    } else if (n == klog->last_len && n > 0) {
        // Same size — check if the tail changed (ring buffer wrapped)
        if (klog->klog_buf[n - 1] != klog->last_tail_byte) {
            klog_refeed(klog, n);
        } else {
            return; // nothing changed
        }
    } else if (n < klog->last_len) {
        // Unexpected shrink — full re-render
        klog_refeed(klog, n);
    }

    klog->last_len = n;
    if (n > 0) klog->last_tail_byte = klog->klog_buf[n - 1];
    win->dirty = true;
}

static void klog_on_close(Window* win) {
    KlogState* klog = (KlogState*)win->app_data;
    if (klog) {
        if (klog->term.cells) montauk::free(klog->term.cells);
        if (klog->term.alt_cells) montauk::free(klog->term.alt_cells);
        if (klog->klog_buf) montauk::mfree(klog->klog_buf);
        montauk::mfree(klog);
        win->app_data = nullptr;
    }
}

// ============================================================================
// Kernel Log launcher
// ============================================================================

void open_klog(DesktopState* ds) {
    int idx = desktop_create_window(ds, "Kernel Log", 160, 60, 720, 480);
    if (idx < 0) return;

    Window* win = &ds->windows[idx];
    Rect cr = win->content_rect();
    int cols = cr.w / mono_cell_width();
    int rows = cr.h / mono_cell_height();

    KlogState* klog = (KlogState*)montauk::malloc(sizeof(KlogState));
    montauk::memset(klog, 0, sizeof(KlogState));

    // Initialize with scrollback enabled
    terminal_init_cells(&klog->term, cols, rows, TERM_MAX_SCROLLBACK);

    // Allocate klog read buffer
    klog->klog_buf = (char*)montauk::malloc(KLOG_READ_SIZE);
    klog->last_len = 0;
    klog->last_tail_byte = 0;
    klog->last_poll_ms = 0;

    // Do an initial read to show existing log content
    int n = (int)montauk::read_klog(klog->klog_buf, KLOG_READ_SIZE);
    if (n > 0) {
        klog_refeed(klog, n);
        klog->last_len = n;
        klog->last_tail_byte = klog->klog_buf[n - 1];
    }

    win->app_data = klog;
    win->on_draw = klog_on_draw;
    win->on_mouse = klog_on_mouse;
    win->on_key = klog_on_key;
    win->on_close = klog_on_close;
    win->on_poll = klog_on_poll;
}
