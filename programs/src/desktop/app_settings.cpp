/*
    * app_settings.cpp
    * ZenithOS Desktop - About application
    * Copyright (c) 2026 Daniel Hammer
*/

#include "apps_common.hpp"

// ============================================================================
// About state and callbacks
// ============================================================================

struct AboutState {
    Zenith::SysInfo sys_info;
    uint64_t uptime_ms;
};

static void about_on_draw(Window* win, Framebuffer& fb) {
    AboutState* st = (AboutState*)win->app_data;
    if (!st) return;

    st->uptime_ms = zenith::get_milliseconds();

    Canvas c(win);
    c.fill(colors::WINDOW_BG);

    Color dim = Color::from_rgb(0x88, 0x88, 0x88);
    int x = 16;
    int y = 20;
    char line[128];
    int line_h = FONT_HEIGHT + 6;

    // OS name in 2x size
    c.text_2x(x, y, st->sys_info.osName, colors::ACCENT);
    y += FONT_HEIGHT * 2 + 8;

    snprintf(line, sizeof(line), "Version %s", st->sys_info.osVersion);
    c.text(x, y, line, colors::TEXT_COLOR);
    y += line_h + 8;

    c.hline(x, y, c.w - 2 * x, colors::BORDER);
    y += 12;

    snprintf(line, sizeof(line), "API version:  %d", (int)st->sys_info.apiVersion);
    c.kv_line(x, &y, line, colors::TEXT_COLOR, line_h);

    int up_sec = (int)(st->uptime_ms / 1000);
    int up_min = up_sec / 60;
    int up_hr = up_min / 60;
    snprintf(line, sizeof(line), "Uptime:       %d:%02d:%02d", up_hr, up_min % 60, up_sec % 60);
    c.kv_line(x, &y, line, colors::TEXT_COLOR, line_h);

    snprintf(line, sizeof(line), "Build:        %s %s", __DATE__, __TIME__);
    c.text(x, y, line, colors::TEXT_COLOR);
    y += line_h + 16;

    c.hline(x, y, c.w - 2 * x, colors::BORDER);
    y += 12;

    c.kv_line(x, &y, "A hobby operating system built from scratch.", dim, line_h);
    c.text(x, y, "Copyright (c) 2026 Daniel Hammer", dim);
}

static void about_on_close(Window* win) {
    if (win->app_data) {
        zenith::mfree(win->app_data);
        win->app_data = nullptr;
    }
}

// ============================================================================
// About launcher
// ============================================================================

void open_settings(DesktopState* ds) {
    int idx = desktop_create_window(ds, "About", 280, 150, 380, 280);
    if (idx < 0) return;

    Window* win = &ds->windows[idx];
    AboutState* st = (AboutState*)zenith::malloc(sizeof(AboutState));
    zenith::memset(st, 0, sizeof(AboutState));
    zenith::get_info(&st->sys_info);
    st->uptime_ms = zenith::get_milliseconds();

    win->app_data = st;
    win->on_draw = about_on_draw;
    win->on_close = about_on_close;
}
