/*
 * main.cpp
 * MontaukOS Process Manager
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

static constexpr int INIT_W          = 620;
static constexpr int INIT_H          = 420;
static constexpr int PM_TOOLBAR_H    = 36;
static constexpr int PM_TAB_H        = 32;
static constexpr int PM_HEADER_H     = 24;
static constexpr int PM_ITEM_H       = 28;
static constexpr int PM_POLL_MS      = 1000;
static constexpr int PM_MAX_PROCS    = 256;
static constexpr int PM_MAX_WINDOWS  = 64;
static constexpr int FONT_SIZE       = 18;

static constexpr Color PM_TOOLBAR_BG = Color::from_rgb(0xF5, 0xF5, 0xF5);
static constexpr Color PM_HEADER_BG  = Color::from_rgb(0xF0, 0xF0, 0xF0);
static constexpr Color PM_BORDER     = Color::from_rgb(0xD0, 0xD0, 0xD0);
static constexpr Color PM_DIM_TEXT   = Color::from_rgb(0x66, 0x66, 0x66);
static constexpr Color PM_SUCCESS    = Color::from_rgb(0x22, 0x88, 0x22);
static constexpr Color PM_WARNING    = Color::from_rgb(0xCC, 0x88, 0x00);
static constexpr Color PM_DANGER     = Color::from_rgb(0xCC, 0x33, 0x33);
static constexpr Color PM_INFO       = Color::from_rgb(0x33, 0x66, 0xCC);

enum ProcMgrTab {
    PM_TAB_PROCESSES = 0,
    PM_TAB_WINDOWS   = 1,
    PM_TAB_COUNT     = 2,
};

static const char* g_tab_labels[PM_TAB_COUNT] = {
    "Processes",
    "Windows",
};

struct ProcMgrState {
    Montauk::ProcInfo procs[PM_MAX_PROCS];
    Montauk::WinInfo windows[PM_MAX_WINDOWS];
    int proc_count;
    int win_count;
    int selected;
    int selected_window;
    int proc_scroll;
    int win_scroll;
    int active_tab;
    uint64_t last_poll_ms;
};

static WsWindow g_win;
static ProcMgrState g_pm = {};

static void format_size(char* buf, uint64_t size) {
    if (size < 1024ULL) {
        snprintf(buf, 24, "%llu B", (unsigned long long)size);
    } else if (size < 1024ULL * 1024ULL) {
        uint64_t kb = size / 1024ULL;
        uint64_t frac = ((size % 1024ULL) * 10ULL) / 1024ULL;
        if (kb < 10ULL) snprintf(buf, 24, "%llu.%llu KB",
                                 (unsigned long long)kb, (unsigned long long)frac);
        else snprintf(buf, 24, "%llu KB", (unsigned long long)kb);
    } else if (size < 1024ULL * 1024ULL * 1024ULL) {
        uint64_t mb = size / (1024ULL * 1024ULL);
        uint64_t frac = ((size % (1024ULL * 1024ULL)) * 10ULL) / (1024ULL * 1024ULL);
        if (mb < 10ULL) snprintf(buf, 24, "%llu.%llu MB",
                                 (unsigned long long)mb, (unsigned long long)frac);
        else snprintf(buf, 24, "%llu MB", (unsigned long long)mb);
    } else {
        uint64_t gb = size / (1024ULL * 1024ULL * 1024ULL);
        uint64_t frac = ((size % (1024ULL * 1024ULL * 1024ULL)) * 10ULL) / (1024ULL * 1024ULL * 1024ULL);
        if (gb < 10ULL) snprintf(buf, 24, "%llu.%llu GB",
                                 (unsigned long long)gb, (unsigned long long)frac);
        else snprintf(buf, 24, "%llu GB", (unsigned long long)gb);
    }
}

static const char* proc_state_label(uint8_t state, Color* out_color) {
    Color color = colors::TEXT_COLOR;
    const char* label = "?";
    switch (state) {
    case 1:
        label = "Ready";
        color = PM_INFO;
        break;
    case 2:
        label = "Run";
        color = PM_SUCCESS;
        break;
    case 3:
        label = "Block";
        color = PM_WARNING;
        break;
    case 4:
        label = "Term";
        color = PM_DANGER;
        break;
    default:
        break;
    }
    if (out_color) *out_color = color;
    return label;
}

static int content_top() {
    return PM_TOOLBAR_H + PM_TAB_H;
}

static int process_list_y() {
    return content_top() + PM_HEADER_H;
}

static int window_list_y() {
    return content_top() + PM_HEADER_H;
}

static int process_visible_rows() {
    int avail = g_win.height - process_list_y();
    if (avail <= 0) return 0;
    return avail / PM_ITEM_H;
}

static int window_visible_rows() {
    int avail = g_win.height - window_list_y();
    if (avail <= 0) return 0;
    return avail / PM_ITEM_H;
}

static int live_process_count() {
    int count = 0;
    for (int i = 0; i < g_pm.proc_count; i++) {
        uint8_t state = g_pm.procs[i].state;
        if (state == 1 || state == 2 || state == 3)
            count++;
    }
    return count;
}

static int find_process_index_by_pid(int pid) {
    for (int i = 0; i < g_pm.proc_count; i++) {
        if (g_pm.procs[i].pid == pid)
            return i;
    }
    return -1;
}

static int find_window_index_by_id(int id) {
    for (int i = 0; i < g_pm.win_count; i++) {
        if (g_pm.windows[i].id == id)
            return i;
    }
    return -1;
}

static void clamp_scrolls() {
    int proc_vis = process_visible_rows();
    int max_proc_scroll = 0;
    if (g_pm.proc_count > proc_vis && proc_vis > 0)
        max_proc_scroll = g_pm.proc_count - proc_vis;
    g_pm.proc_scroll = gui_clamp(g_pm.proc_scroll, 0, max_proc_scroll);

    int win_vis = window_visible_rows();
    int max_win_scroll = 0;
    if (g_pm.win_count > win_vis && win_vis > 0)
        max_win_scroll = g_pm.win_count - win_vis;
    g_pm.win_scroll = gui_clamp(g_pm.win_scroll, 0, max_win_scroll);
}

static void ensure_process_selection_visible() {
    if (g_pm.selected < 0) return;
    int visible = process_visible_rows();
    if (visible <= 0) return;
    if (g_pm.selected < g_pm.proc_scroll)
        g_pm.proc_scroll = g_pm.selected;
    else if (g_pm.selected >= g_pm.proc_scroll + visible)
        g_pm.proc_scroll = g_pm.selected - visible + 1;
    clamp_scrolls();
}

static void ensure_window_selection_visible() {
    if (g_pm.selected_window < 0) return;
    int visible = window_visible_rows();
    if (visible <= 0) return;
    if (g_pm.selected_window < g_pm.win_scroll)
        g_pm.win_scroll = g_pm.selected_window;
    else if (g_pm.selected_window >= g_pm.win_scroll + visible)
        g_pm.win_scroll = g_pm.selected_window - visible + 1;
    clamp_scrolls();
}

static bool refresh_state(bool force) {
    uint64_t now = montauk::get_milliseconds();
    if (!force && now - g_pm.last_poll_ms < PM_POLL_MS)
        return false;

    g_pm.last_poll_ms = now;

    int prev_pid = -1;
    if (g_pm.selected >= 0 && g_pm.selected < g_pm.proc_count)
        prev_pid = g_pm.procs[g_pm.selected].pid;

    int prev_win_id = -1;
    if (g_pm.selected_window >= 0 && g_pm.selected_window < g_pm.win_count)
        prev_win_id = g_pm.windows[g_pm.selected_window].id;

    g_pm.proc_count = montauk::proclist(g_pm.procs, PM_MAX_PROCS);
    if (g_pm.proc_count < 0) g_pm.proc_count = 0;

    g_pm.selected = find_process_index_by_pid(prev_pid);

    g_pm.win_count = montauk::win_enumerate(g_pm.windows, PM_MAX_WINDOWS);
    if (g_pm.win_count < 0) g_pm.win_count = 0;
    g_pm.selected_window = find_window_index_by_id(prev_win_id);

    clamp_scrolls();
    ensure_process_selection_visible();
    ensure_window_selection_visible();
    return true;
}

static bool can_kill_selected() {
    return g_pm.active_tab == PM_TAB_PROCESSES
        && g_pm.selected >= 0
        && g_pm.selected < g_pm.proc_count
        && g_pm.procs[g_pm.selected].pid != 0
        && g_pm.procs[g_pm.selected].state != 4;
}

static void kill_selected() {
    if (!can_kill_selected())
        return;

    montauk::kill(g_pm.procs[g_pm.selected].pid);
    refresh_state(true);
}

static void cycle_tab(int delta) {
    int next = g_pm.active_tab + delta;
    while (next < 0) next += PM_TAB_COUNT;
    while (next >= PM_TAB_COUNT) next -= PM_TAB_COUNT;
    g_pm.active_tab = next;
    clamp_scrolls();
}

static void draw_text_fit(Canvas& c, TrueTypeFont* font, int x, int y,
                          const char* text, int max_w, Color color, int size) {
    if (!text || !text[0] || max_w <= 4) return;
    if (text_width(font, text, size) <= max_w) {
        draw_text(c, font, x, y, text, color, size);
        return;
    }

    const char* ellipsis = "...";
    int ell_w = text_width(font, ellipsis, size);
    if (ell_w >= max_w) return;

    char buf[96];
    int out = 0;
    while (text[out] && out < (int)sizeof(buf) - 4) {
        buf[out] = text[out];
        buf[out + 1] = '\0';
        if (text_width(font, buf, size) + ell_w > max_w)
            break;
        out++;
    }

    if (out <= 0) return;
    buf[out] = '.';
    buf[out + 1] = '.';
    buf[out + 2] = '.';
    buf[out + 3] = '\0';
    draw_text(c, font, x, y, buf, color, size);
}

static void render_toolbar(Canvas& c, TrueTypeFont* font, int fh) {
    c.fill_rect(0, 0, g_win.width, PM_TOOLBAR_H, PM_TOOLBAR_BG);
    c.hline(0, PM_TOOLBAR_H - 1, g_win.width, PM_BORDER);

    int btn_h = 26;
    int btn_y = (PM_TOOLBAR_H - btn_h) / 2;
    draw_button(c, font, 8, btn_y, 82, btn_h, "Refresh",
                Color::from_rgb(0xE3, 0xE8, 0xEF), colors::TEXT_COLOR, 4, FONT_SIZE);

    Color kill_bg = can_kill_selected() ? PM_DANGER : Color::from_rgb(0xAA, 0xAA, 0xAA);
    draw_button(c, font, 98, btn_y, 110, btn_h, "End Process",
                kill_bg, colors::WHITE, 4, FONT_SIZE);

    char status[96];
    if (g_pm.active_tab == PM_TAB_PROCESSES) {
        snprintf(status, sizeof(status), "%d shown, %d live",
                 g_pm.proc_count, live_process_count());
    } else {
        snprintf(status, sizeof(status), "%d windows", g_pm.win_count);
    }
    int status_w = text_width(font, status, FONT_SIZE);
    draw_text(c, font, g_win.width - status_w - 12, (PM_TOOLBAR_H - fh) / 2,
              status, colors::TEXT_COLOR, FONT_SIZE);
}

static void render_tabs(Canvas& c, TrueTypeFont* font, int fh) {
    int y = PM_TOOLBAR_H;
    c.fill_rect(0, y, g_win.width, PM_TAB_H, PM_TOOLBAR_BG);
    c.hline(0, y + PM_TAB_H - 1, g_win.width, PM_BORDER);

    int tab_w = g_win.width / PM_TAB_COUNT;
    for (int i = 0; i < PM_TAB_COUNT; i++) {
        int tx = i * tab_w;
        bool active = (i == g_pm.active_tab);
        if (active) {
            c.fill_rect(tx, y, tab_w, PM_TAB_H, colors::WINDOW_BG);
            c.fill_rect(tx + 4, y + PM_TAB_H - 3, tab_w - 8, 3, colors::ACCENT);
        }
        int tw = text_width(font, g_tab_labels[i], FONT_SIZE);
        Color color = active ? colors::ACCENT : PM_DIM_TEXT;
        draw_text(c, font, tx + (tab_w - tw) / 2, y + (PM_TAB_H - fh) / 2,
                  g_tab_labels[i], color, FONT_SIZE);
    }
}

static void render_processes(Canvas& c, TrueTypeFont* font, int fh) {
    int header_y = content_top();
    c.fill_rect(0, header_y, g_win.width, PM_HEADER_H, PM_HEADER_BG);
    c.hline(0, header_y + PM_HEADER_H - 1, g_win.width, PM_BORDER);

    int col_pid = 12;
    int col_ppid = 64;
    int col_name = 124;
    int col_heap = g_win.width - 150;
    int col_state = g_win.width - 70;
    int header_text_y = header_y + (PM_HEADER_H - fh) / 2;

    draw_text(c, font, col_pid, header_text_y, "PID", PM_DIM_TEXT, FONT_SIZE);
    draw_text(c, font, col_ppid, header_text_y, "PPID", PM_DIM_TEXT, FONT_SIZE);
    draw_text(c, font, col_name, header_text_y, "Name", PM_DIM_TEXT, FONT_SIZE);
    draw_text(c, font, col_heap, header_text_y, "Heap", PM_DIM_TEXT, FONT_SIZE);
    draw_text(c, font, col_state, header_text_y, "State", PM_DIM_TEXT, FONT_SIZE);

    int list_y = process_list_y();
    int visible = process_visible_rows();
    for (int row = 0; row < visible; row++) {
        int index = g_pm.proc_scroll + row;
        if (index >= g_pm.proc_count) break;

        int row_y = list_y + row * PM_ITEM_H;
        if (index == g_pm.selected)
            c.fill_rect(0, row_y, g_win.width, PM_ITEM_H, colors::MENU_HOVER);

        int text_y = row_y + (PM_ITEM_H - fh) / 2;
        char buf[32];

        snprintf(buf, sizeof(buf), "%d", (int)g_pm.procs[index].pid);
        draw_text(c, font, col_pid, text_y, buf, colors::TEXT_COLOR, FONT_SIZE);

        snprintf(buf, sizeof(buf), "%d", (int)g_pm.procs[index].parentPid);
        draw_text(c, font, col_ppid, text_y, buf, colors::TEXT_COLOR, FONT_SIZE);

        draw_text_fit(c, font, col_name, text_y, g_pm.procs[index].name,
                      col_heap - col_name - 10, colors::TEXT_COLOR, FONT_SIZE);

        format_size(buf, g_pm.procs[index].heapUsed);
        draw_text(c, font, col_heap, text_y, buf, colors::TEXT_COLOR, FONT_SIZE);

        Color state_color;
        const char* state = proc_state_label(g_pm.procs[index].state, &state_color);
        draw_text(c, font, col_state, text_y, state, state_color, FONT_SIZE);
    }
}

static void render_windows(Canvas& c, TrueTypeFont* font, int fh) {
    int header_y = content_top();
    c.fill_rect(0, header_y, g_win.width, PM_HEADER_H, PM_HEADER_BG);
    c.hline(0, header_y + PM_HEADER_H - 1, g_win.width, PM_BORDER);

    int col_id = 12;
    int col_pid = 60;
    int col_title = 118;
    int col_size = g_win.width - 130;
    int col_dirty = g_win.width - 52;
    int header_text_y = header_y + (PM_HEADER_H - fh) / 2;

    draw_text(c, font, col_id, header_text_y, "ID", PM_DIM_TEXT, FONT_SIZE);
    draw_text(c, font, col_pid, header_text_y, "PID", PM_DIM_TEXT, FONT_SIZE);
    draw_text(c, font, col_title, header_text_y, "Title", PM_DIM_TEXT, FONT_SIZE);
    draw_text(c, font, col_size, header_text_y, "Size", PM_DIM_TEXT, FONT_SIZE);
    draw_text(c, font, col_dirty, header_text_y, "D", PM_DIM_TEXT, FONT_SIZE);

    int list_y = window_list_y();
    int visible = window_visible_rows();
    for (int row = 0; row < visible; row++) {
        int index = g_pm.win_scroll + row;
        if (index >= g_pm.win_count) break;

        int row_y = list_y + row * PM_ITEM_H;
        if (index == g_pm.selected_window)
            c.fill_rect(0, row_y, g_win.width, PM_ITEM_H, colors::MENU_HOVER);

        int text_y = row_y + (PM_ITEM_H - fh) / 2;
        char buf[64];

        snprintf(buf, sizeof(buf), "%d", (int)g_pm.windows[index].id);
        draw_text(c, font, col_id, text_y, buf, colors::TEXT_COLOR, FONT_SIZE);

        snprintf(buf, sizeof(buf), "%d", (int)g_pm.windows[index].ownerPid);
        draw_text(c, font, col_pid, text_y, buf, colors::TEXT_COLOR, FONT_SIZE);

        draw_text_fit(c, font, col_title, text_y, g_pm.windows[index].title,
                      col_size - col_title - 10, colors::TEXT_COLOR, FONT_SIZE);

        snprintf(buf, sizeof(buf), "%dx%d", (int)g_pm.windows[index].width, (int)g_pm.windows[index].height);
        draw_text(c, font, col_size, text_y, buf, colors::TEXT_COLOR, FONT_SIZE);

        draw_text(c, font, col_dirty, text_y, g_pm.windows[index].dirty ? "Y" : "N",
                  g_pm.windows[index].dirty ? PM_WARNING : PM_DIM_TEXT, FONT_SIZE);
    }
}

static void render() {
    TrueTypeFont* font = fonts::system_font;
    Canvas c = g_win.canvas();
    c.fill(colors::WINDOW_BG);

    int fh = text_height(font, FONT_SIZE);
    render_toolbar(c, font, fh);
    render_tabs(c, font, fh);

    switch (g_pm.active_tab) {
    case PM_TAB_PROCESSES:
        render_processes(c, font, fh);
        break;
    case PM_TAB_WINDOWS:
        render_windows(c, font, fh);
        break;
    }
}

static bool handle_toolbar_click(int mx, int my) {
    int btn_h = 26;
    int btn_y = (PM_TOOLBAR_H - btn_h) / 2;
    Rect refresh_rect = {8, btn_y, 82, btn_h};
    Rect kill_rect = {98, btn_y, 110, btn_h};

    if (refresh_rect.contains(mx, my)) {
        refresh_state(true);
        return true;
    }

    if (kill_rect.contains(mx, my)) {
        kill_selected();
        return true;
    }

    return false;
}

static bool handle_tab_click(int mx, int my) {
    if (my < PM_TOOLBAR_H || my >= PM_TOOLBAR_H + PM_TAB_H)
        return false;

    int tab_w = g_win.width / PM_TAB_COUNT;
    int tab = mx / tab_w;
    if (tab >= 0 && tab < PM_TAB_COUNT) {
        g_pm.active_tab = tab;
        return true;
    }
    return false;
}

static bool handle_process_click(int my) {
    if (my < process_list_y())
        return false;

    int row = (my - process_list_y()) / PM_ITEM_H;
    int index = g_pm.proc_scroll + row;
    if (index >= 0 && index < g_pm.proc_count)
        g_pm.selected = index;
    else
        g_pm.selected = -1;
    return true;
}

static bool handle_window_click(int my) {
    if (my < window_list_y())
        return false;

    int row = (my - window_list_y()) / PM_ITEM_H;
    int index = g_pm.win_scroll + row;
    if (index >= 0 && index < g_pm.win_count)
        g_pm.selected_window = index;
    else
        g_pm.selected_window = -1;
    return true;
}

static bool handle_mouse(const Montauk::WinEvent& ev) {
    if (ev.mouse.scroll != 0) {
        if (g_pm.active_tab == PM_TAB_PROCESSES) {
            g_pm.proc_scroll -= ev.mouse.scroll * 3;
            clamp_scrolls();
            return true;
        }
        if (g_pm.active_tab == PM_TAB_WINDOWS) {
            g_pm.win_scroll -= ev.mouse.scroll * 3;
            clamp_scrolls();
            return true;
        }
    }

    bool left_pressed = (ev.mouse.buttons & 1) && !(ev.mouse.prev_buttons & 1);
    if (!left_pressed)
        return false;

    int mx = ev.mouse.x;
    int my = ev.mouse.y;

    if (my < PM_TOOLBAR_H)
        return handle_toolbar_click(mx, my);
    if (my < PM_TOOLBAR_H + PM_TAB_H)
        return handle_tab_click(mx, my);

    if (g_pm.active_tab == PM_TAB_PROCESSES)
        return handle_process_click(my);
    if (g_pm.active_tab == PM_TAB_WINDOWS)
        return handle_window_click(my);
    return false;
}

static bool handle_key(const Montauk::KeyEvent& key) {
    if (!key.pressed)
        return false;

    if (key.scancode == 0x0F) {
        cycle_tab(key.shift ? -1 : 1);
        return true;
    }

    if (key.scancode == 0x4B) {
        cycle_tab(-1);
        return true;
    }

    if (key.scancode == 0x4D) {
        cycle_tab(1);
        return true;
    }

    if (key.scancode == 0x53) {
        if (can_kill_selected()) {
            kill_selected();
            return true;
        }
    }

    if (key.scancode == 0x48) {
        if (g_pm.active_tab == PM_TAB_PROCESSES) {
            if (g_pm.selected > 0) g_pm.selected--;
            else if (g_pm.proc_count > 0) g_pm.selected = 0;
            ensure_process_selection_visible();
            return true;
        }
        if (g_pm.active_tab == PM_TAB_WINDOWS) {
            if (g_pm.selected_window > 0) g_pm.selected_window--;
            else if (g_pm.win_count > 0) g_pm.selected_window = 0;
            ensure_window_selection_visible();
            return true;
        }
    }

    if (key.scancode == 0x50) {
        if (g_pm.active_tab == PM_TAB_PROCESSES) {
            if (g_pm.selected < g_pm.proc_count - 1) g_pm.selected++;
            else if (g_pm.selected < 0 && g_pm.proc_count > 0) g_pm.selected = 0;
            ensure_process_selection_visible();
            return true;
        }
        if (g_pm.active_tab == PM_TAB_WINDOWS) {
            if (g_pm.selected_window < g_pm.win_count - 1) g_pm.selected_window++;
            else if (g_pm.selected_window < 0 && g_pm.win_count > 0) g_pm.selected_window = 0;
            ensure_window_selection_visible();
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

    montauk::memset(&g_pm, 0, sizeof(g_pm));
    g_pm.selected = -1;
    g_pm.selected_window = -1;
    g_pm.active_tab = PM_TAB_PROCESSES;
    refresh_state(true);
    render();
    g_win.present();

    while (!g_win.closed) {
        bool redraw = refresh_state(false);

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
