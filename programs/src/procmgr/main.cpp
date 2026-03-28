/*
 * main.cpp
 * MontaukOS Process Manager - standalone Window Server app
 * Preserves the old desktop-integrated Processes window layout
 * Copyright (c) 2026 Daniel Hammer
 */

#include <montauk/syscall.h>
#include <montauk/string.h>
#include <montauk/heap.h>
#include <gui/gui.hpp>
#include <gui/canvas.hpp>
#include <gui/standalone.hpp>
#include <gui/truetype.hpp>

extern "C" {
#include <stdio.h>
}

using namespace gui;

static constexpr int INIT_W       = 520;
static constexpr int INIT_H       = 400;
static constexpr int PM_TOOLBAR_H = 36;
static constexpr int PM_HEADER_H  = 24;
static constexpr int PM_ITEM_H    = 28;
static constexpr int PM_MAX_PROCS = 16;
static constexpr int PM_POLL_MS   = 1000;
static constexpr int FONT_SIZE    = 18;

struct ProcMgrState {
    Montauk::ProcInfo procs[PM_MAX_PROCS];
    int proc_count;
    int selected;
    uint64_t last_poll_ms;
};

static WsWindow g_win;
static ProcMgrState g_pm = {};

static void format_size(char* buf, int size) {
    if (size < 1024) {
        snprintf(buf, 16, "%d B", size);
    } else if (size < 1024 * 1024) {
        int kb = size / 1024;
        int frac = ((size % 1024) * 10) / 1024;
        if (kb < 10) snprintf(buf, 16, "%d.%d KB", kb, frac);
        else snprintf(buf, 16, "%d KB", kb);
    } else {
        int mb = size / (1024 * 1024);
        int frac = ((size % (1024 * 1024)) * 10) / (1024 * 1024);
        if (mb < 10) snprintf(buf, 16, "%d.%d MB", mb, frac);
        else snprintf(buf, 16, "%d MB", mb);
    }
}

static bool refresh_processes(bool force) {
    uint64_t now = montauk::get_milliseconds();
    if (!force && now - g_pm.last_poll_ms < PM_POLL_MS)
        return false;

    g_pm.last_poll_ms = now;

    int prev_pid = -1;
    if (g_pm.selected >= 0 && g_pm.selected < g_pm.proc_count)
        prev_pid = g_pm.procs[g_pm.selected].pid;

    g_pm.proc_count = montauk::proclist(g_pm.procs, PM_MAX_PROCS);
    g_pm.selected = -1;

    if (prev_pid >= 0) {
        for (int i = 0; i < g_pm.proc_count; i++) {
            if (g_pm.procs[i].pid == prev_pid) {
                g_pm.selected = i;
                break;
            }
        }
    }

    return true;
}

static bool can_kill_selected() {
    return g_pm.selected >= 0
        && g_pm.selected < g_pm.proc_count
        && g_pm.procs[g_pm.selected].pid != 0;
}

static void kill_selected() {
    if (!can_kill_selected())
        return;

    montauk::kill(g_pm.procs[g_pm.selected].pid);
    refresh_processes(true);
}

static void render() {
    TrueTypeFont* font = fonts::system_font;
    Canvas c = g_win.canvas();
    c.fill(colors::WINDOW_BG);

    int fh = text_height(font, FONT_SIZE);

    c.fill_rect(0, 0, g_win.width, PM_TOOLBAR_H, Color::from_rgb(0xF5, 0xF5, 0xF5));
    c.hline(0, PM_TOOLBAR_H - 1, g_win.width, colors::BORDER);

    int btn_w = 100;
    int btn_h = 26;
    int btn_x = 8;
    int btn_y = (PM_TOOLBAR_H - btn_h) / 2;
    Color btn_bg = can_kill_selected()
        ? Color::from_rgb(0xCC, 0x33, 0x33)
        : Color::from_rgb(0xAA, 0xAA, 0xAA);
    draw_button(c, font, btn_x, btn_y, btn_w, btn_h, "End Process",
                btn_bg, colors::WHITE, 4, FONT_SIZE);

    char count_str[24];
    snprintf(count_str, sizeof(count_str), "%d processes", g_pm.proc_count);
    int count_w = text_width(font, count_str, FONT_SIZE);
    draw_text(c, font, g_win.width - count_w - 12, (PM_TOOLBAR_H - fh) / 2,
              count_str, colors::TEXT_COLOR, FONT_SIZE);

    int header_y = PM_TOOLBAR_H;
    c.fill_rect(0, header_y, g_win.width, PM_HEADER_H, Color::from_rgb(0xF0, 0xF0, 0xF0));

    int col_pid = 12;
    int col_name = 64;
    int col_mem = g_win.width - 120;
    int col_state = g_win.width - 52;
    int header_text_y = header_y + (PM_HEADER_H - fh) / 2;
    Color header_color = Color::from_rgb(0x66, 0x66, 0x66);

    draw_text(c, font, col_pid, header_text_y, "PID", header_color, FONT_SIZE);
    draw_text(c, font, col_name, header_text_y, "Name", header_color, FONT_SIZE);
    draw_text(c, font, col_mem, header_text_y, "Mem", header_color, FONT_SIZE);
    draw_text(c, font, col_state, header_text_y, "St", header_color, FONT_SIZE);
    c.hline(0, header_y + PM_HEADER_H - 1, g_win.width, colors::BORDER);

    int list_y = PM_TOOLBAR_H + PM_HEADER_H;
    for (int i = 0; i < g_pm.proc_count; i++) {
        int row_y = list_y + i * PM_ITEM_H;
        if (row_y + PM_ITEM_H > g_win.height)
            break;

        if (i == g_pm.selected)
            c.fill_rect(0, row_y, g_win.width, PM_ITEM_H, colors::MENU_HOVER);

        int text_y = row_y + (PM_ITEM_H - fh) / 2;

        char pid_str[8];
        snprintf(pid_str, sizeof(pid_str), "%d", (int)g_pm.procs[i].pid);
        int pid_w = text_width(font, pid_str, FONT_SIZE);
        draw_text(c, font, col_pid + 32 - pid_w, text_y, pid_str, colors::TEXT_COLOR, FONT_SIZE);

        draw_text(c, font, col_name, text_y, g_pm.procs[i].name, colors::TEXT_COLOR, FONT_SIZE);

        char mem_str[16];
        format_size(mem_str, (int)g_pm.procs[i].heapUsed);
        draw_text(c, font, col_mem, text_y, mem_str, colors::TEXT_COLOR, FONT_SIZE);

        const char* state_str = "?";
        Color state_color = colors::TEXT_COLOR;
        switch (g_pm.procs[i].state) {
        case 1:
            state_str = "Rdy";
            state_color = Color::from_rgb(0x33, 0x66, 0xCC);
            break;
        case 2:
            state_str = "Run";
            state_color = Color::from_rgb(0x22, 0x88, 0x22);
            break;
        case 3:
            state_str = "Term";
            state_color = Color::from_rgb(0xCC, 0x33, 0x33);
            break;
        }
        draw_text(c, font, col_state, text_y, state_str, state_color, FONT_SIZE);
    }
}

static bool handle_mouse(const Montauk::WinEvent& ev) {
    bool left_pressed = (ev.mouse.buttons & 1) && !(ev.mouse.prev_buttons & 1);
    if (!left_pressed)
        return false;

    int mx = ev.mouse.x;
    int my = ev.mouse.y;

    int btn_w = 100;
    int btn_h = 26;
    int btn_x = 8;
    int btn_y = (PM_TOOLBAR_H - btn_h) / 2;
    Rect btn_rect = {btn_x, btn_y, btn_w, btn_h};
    if (btn_rect.contains(mx, my)) {
        kill_selected();
        return true;
    }

    int list_y = PM_TOOLBAR_H + PM_HEADER_H;
    if (my < list_y)
        return false;

    int row = (my - list_y) / PM_ITEM_H;
    if (row >= 0 && row < g_pm.proc_count) g_pm.selected = row;
    else g_pm.selected = -1;
    return true;
}

static bool handle_key(const Montauk::KeyEvent& key) {
    if (!key.pressed)
        return false;

    if (key.scancode == 0x48) {
        if (g_pm.selected > 0) g_pm.selected--;
        else if (g_pm.proc_count > 0) g_pm.selected = 0;
        return true;
    }

    if (key.scancode == 0x50) {
        if (g_pm.selected < g_pm.proc_count - 1) g_pm.selected++;
        return true;
    }

    if (key.scancode == 0x53) {
        if (can_kill_selected()) {
            kill_selected();
            return true;
        }
    }

    return false;
}

extern "C" void _start() {
    if (!fonts::init())
        montauk::exit(1);

    if (!g_win.create("Processes", INIT_W, INIT_H))
        montauk::exit(1);

    g_pm.selected = -1;
    refresh_processes(true);
    render();
    g_win.present();

    while (!g_win.closed) {
        bool redraw = refresh_processes(false);

        Montauk::WinEvent ev;
        int r = g_win.poll(&ev);
        if (r < 0) break;

        if (r == 0) {
            if (redraw) {
                render();
                g_win.present();
            }
            montauk::sleep_ms(16);
            continue;
        }

        if (ev.type == 3)
            break;

        if (ev.type == 1) redraw |= handle_mouse(ev);
        else if (ev.type == 0) redraw |= handle_key(ev.key);
        else if (ev.type == 2 || ev.type == 4) redraw = true;

        if (redraw) {
            render();
            g_win.present();
        }
    }

    g_win.destroy();
    montauk::exit(0);
}
