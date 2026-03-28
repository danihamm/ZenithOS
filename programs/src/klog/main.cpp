/*
 * main.cpp
 * MontaukOS Kernel Log - standalone Window Server app
 * Preserves the old desktop-integrated log viewer layout
 * Copyright (c) 2026 Daniel Hammer
 */

#include <montauk/syscall.h>
#include <montauk/string.h>
#include <montauk/heap.h>
#include <gui/gui.hpp>
#include <gui/canvas.hpp>
#include <gui/standalone.hpp>
#include <gui/terminal.hpp>
#include <gui/truetype.hpp>

using namespace gui;

static constexpr int INIT_W = 718;
static constexpr int INIT_H = 449;

static constexpr int KLOG_READ_SIZE = 65536;
static constexpr int KLOG_POLL_MS   = 250;

struct KlogState {
    TerminalState term;
    char* klog_buf;
    int last_len;
    char last_tail_byte;
    uint64_t last_poll_ms;
    uint32_t* last_pixels;
};

static WsWindow g_win;
static KlogState g_klog = {};

static void klog_refeed(int n) {
    int lines_needed = g_klog.term.rows + g_klog.term.max_scrollback;
    int start = 0;
    int line_count = 0;
    for (int i = n - 1; i >= 0; i--) {
        if (g_klog.klog_buf[i] == '\n') {
            line_count++;
            if (line_count >= lines_needed) {
                start = i + 1;
                break;
            }
        }
    }

    g_klog.term.scrollback_lines = 0;
    g_klog.term.view_offset = 0;
    g_klog.term.cursor_x = 0;
    g_klog.term.cursor_y = 0;
    g_klog.term.current_fg = colors::TERM_FG;
    g_klog.term.current_bg = colors::TERM_BG;

    TermCell* screen = term_screen_row(&g_klog.term, 0);
    int total = g_klog.term.cols * g_klog.term.rows;
    for (int i = 0; i < total; i++)
        screen[i] = {' ', colors::TERM_FG, colors::TERM_BG};

    terminal_feed(&g_klog.term, g_klog.klog_buf + start, n - start);
}

static bool klog_poll() {
    uint64_t now = montauk::get_milliseconds();
    if (now - g_klog.last_poll_ms < KLOG_POLL_MS) return false;
    g_klog.last_poll_ms = now;

    int n = (int)montauk::read_klog(g_klog.klog_buf, KLOG_READ_SIZE);
    if (n <= 0 && g_klog.last_len <= 0) return false;

    if (n > g_klog.last_len) {
        terminal_feed(&g_klog.term, g_klog.klog_buf + g_klog.last_len, n - g_klog.last_len);
    } else if (n == g_klog.last_len && n > 0) {
        if (g_klog.klog_buf[n - 1] != g_klog.last_tail_byte) {
            klog_refeed(n);
        } else {
            return false;
        }
    } else if (n < g_klog.last_len) {
        klog_refeed(n);
    }

    g_klog.last_len = n;
    if (n > 0) g_klog.last_tail_byte = g_klog.klog_buf[n - 1];
    return true;
}

static bool klog_render() {
    if (!g_win.pixels || !g_klog.term.cells) return false;

    int new_cols = g_win.width / mono_cell_width();
    int new_rows = g_win.height / mono_cell_height();
    if (new_cols < 1) new_cols = 1;
    if (new_rows < 1) new_rows = 1;

    if (new_cols != g_klog.term.cols || new_rows != g_klog.term.rows) {
        terminal_resize(&g_klog.term, new_cols, new_rows);
        if (g_klog.last_len > 0)
            klog_refeed(g_klog.last_len);
    }

    if (g_win.pixels != g_klog.last_pixels) {
        g_klog.last_pixels = g_win.pixels;
        g_klog.term.dirty = true;
    }

    if (!g_klog.term.dirty) return false;

    bool was_dirty = g_klog.term.dirty;
    terminal_render(&g_klog.term, g_win.pixels, g_win.width, g_win.height);

    if (was_dirty && g_klog.term.scrollback_lines > 0) {
        int cell_h = mono_cell_height();
        int total_rows = g_klog.term.scrollback_lines + g_klog.term.rows;
        int total_h = total_rows * cell_h;
        int view_h = g_win.height;
        if (total_h > view_h) {
            Canvas c = g_win.canvas();
            int sb_w = 4;
            int sb_x = g_win.width - sb_w - 2;
            int thumb_h = (view_h * view_h) / total_h;
            if (thumb_h < 20) thumb_h = 20;
            int scroll_from_top = g_klog.term.scrollback_lines - g_klog.term.view_offset;
            int max_scroll = g_klog.term.scrollback_lines;
            int thumb_y = (max_scroll > 0)
                ? (scroll_from_top * (view_h - thumb_h)) / max_scroll : 0;
            c.fill_rect(sb_x, 0, sb_w, view_h, Color::from_rgb(0x33, 0x33, 0x33));
            c.fill_rect(sb_x, thumb_y, sb_w, thumb_h, Color::from_rgb(0x88, 0x88, 0x88));
        }
    }

    return true;
}

static void klog_handle_mouse(const Montauk::WinEvent& ev) {
    if (ev.mouse.scroll == 0) return;

    g_klog.term.view_offset += (ev.mouse.scroll < 0) ? 3 : -3;
    if (g_klog.term.view_offset < 0) g_klog.term.view_offset = 0;
    if (g_klog.term.view_offset > g_klog.term.scrollback_lines)
        g_klog.term.view_offset = g_klog.term.scrollback_lines;
    g_klog.term.dirty = true;
}

static void klog_cleanup() {
    if (g_klog.term.cells) montauk::free(g_klog.term.cells);
    if (g_klog.term.alt_cells) montauk::free(g_klog.term.alt_cells);
    if (g_klog.klog_buf) montauk::mfree(g_klog.klog_buf);
    montauk::memset(&g_klog, 0, sizeof(g_klog));
}

extern "C" void _start() {
    if (!fonts::init())
        montauk::exit(1);

    if (!g_win.create("Kernel Log", INIT_W, INIT_H))
        montauk::exit(1);

    int cols = g_win.width / mono_cell_width();
    int rows = g_win.height / mono_cell_height();
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    terminal_init_cells(&g_klog.term, cols, rows, TERM_MAX_SCROLLBACK);
    if (!g_klog.term.cells || !g_klog.term.alt_cells) {
        g_win.destroy();
        montauk::exit(1);
    }

    g_klog.klog_buf = (char*)montauk::malloc(KLOG_READ_SIZE);
    if (!g_klog.klog_buf) {
        klog_cleanup();
        g_win.destroy();
        montauk::exit(1);
    }

    g_klog.last_pixels = g_win.pixels;

    int n = (int)montauk::read_klog(g_klog.klog_buf, KLOG_READ_SIZE);
    if (n > 0) {
        klog_refeed(n);
        g_klog.last_len = n;
        g_klog.last_tail_byte = g_klog.klog_buf[n - 1];
    }

    if (klog_render())
        g_win.present();

    while (true) {
        bool redraw = klog_poll();

        Montauk::WinEvent ev;
        bool quit = false;
        int r = 0;

        while ((r = g_win.poll(&ev)) > 0) {
            redraw = true;

            if (ev.type == 3) {
                quit = true;
                break;
            }

            if (ev.type == 1) {
                klog_handle_mouse(ev);
            } else if (ev.type == 2 || ev.type == 4) {
                g_klog.term.dirty = true;
            }
        }

        if (r < 0 || quit) break;

        if (redraw && klog_render())
            g_win.present();

        if (r == 0)
            montauk::sleep_ms(16);
    }

    klog_cleanup();
    g_win.destroy();
    montauk::exit(0);
}
