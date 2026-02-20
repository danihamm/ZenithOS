/*
    * main.cpp
    * ZenithOS Desktop Environment - window manager, compositor, and run loop
    * Copyright (c) 2026 Daniel Hammer
*/

#include "apps_common.hpp"

// ============================================================================
// Desktop Implementation
// ============================================================================

static const char* month_names[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

void gui::desktop_init(DesktopState* ds) {
    ds->screen_w = ds->fb.width();
    ds->screen_h = ds->fb.height();

    // Immediately clear the screen to hide flanterm boot text
    ds->fb.clear(colors::DESKTOP_BG);
    ds->fb.flip();

    ds->window_count = 0;
    ds->focused_window = -1;
    ds->prev_buttons = 0;
    ds->app_menu_open = false;

    zenith::memset(&ds->mouse, 0, sizeof(Zenith::MouseState));
    zenith::set_mouse_bounds(ds->screen_w - 1, ds->screen_h - 1);

    // Load SVG icons — scalable (colorful) for app menu, symbolic for toolbar/panel
    Color defColor = colors::ICON_COLOR;
    ds->icon_terminal    = svg_load("0:/icons/utilities-terminal.svg",        20, 20, defColor);
    ds->icon_filemanager = svg_load("0:/icons/system-file-manager.svg",      20, 20, defColor);
    ds->icon_sysinfo     = svg_load("0:/icons/preferences-desktop-apps.svg", 20, 20, defColor);
    ds->icon_appmenu     = svg_load("0:/icons/view-app-grid-symbolic.svg",   20, 20, colors::PANEL_TEXT);
    ds->icon_folder      = svg_load("0:/icons/folder.svg",                   16, 16, defColor);
    ds->icon_file        = svg_load("0:/icons/text-x-generic.svg",           16, 16, defColor);
    ds->icon_computer    = svg_load("0:/icons/computer.svg",                 20, 20, defColor);
    ds->icon_network     = svg_load("0:/icons/network-wired-symbolic.svg",   16, 16, colors::PANEL_TEXT);
    ds->icon_calculator  = svg_load("0:/icons/accessories-calculator.svg",   20, 20, defColor);
    ds->icon_texteditor  = svg_load("0:/icons/accessories-text-editor.svg",  20, 20, defColor);
    ds->icon_go_up       = svg_load("0:/icons/go-up-symbolic.svg",           16, 16, defColor);
    ds->icon_go_back     = svg_load("0:/icons/go-previous-symbolic.svg",     16, 16, defColor);
    ds->icon_go_forward  = svg_load("0:/icons/go-next-symbolic.svg",         16, 16, defColor);
    ds->icon_save        = svg_load("0:/icons/document-save-symbolic.svg",   16, 16, defColor);
    ds->icon_home        = svg_load("0:/icons/user-home.svg",                16, 16, defColor);
    ds->icon_exec        = svg_load("0:/icons/utilities-terminal.svg",        16, 16, defColor);
    ds->icon_wikipedia   = svg_load("0:/icons/web-browser.svg",              20, 20, defColor);

    ds->icon_folder_lg = svg_load("0:/icons/folder.svg",                   48, 48, defColor);
    ds->icon_file_lg   = svg_load("0:/icons/text-x-generic.svg",           48, 48, defColor);
    ds->icon_exec_lg   = svg_load("0:/icons/utilities-terminal.svg",        48, 48, defColor);

    ds->icon_settings = svg_load("0:/icons/help-about.svg",     20, 20, defColor);
    ds->icon_reboot   = svg_load("0:/icons/system-reboot.svg", 20, 20, defColor);

    ds->ctx_menu_open = false;
    ds->ctx_menu_x = 0;
    ds->ctx_menu_y = 0;

    ds->net_popup_open = false;
    zenith::get_netcfg(&ds->cached_net_cfg);
    ds->net_cfg_last_poll = zenith::get_milliseconds();
    ds->net_icon_rect = {0, 0, 0, 0};

}

int gui::desktop_create_window(DesktopState* ds, const char* title, int x, int y, int w, int h) {
    if (ds->window_count >= MAX_WINDOWS) return -1;

    int idx = ds->window_count;
    Window* win = &ds->windows[idx];
    zenith::memset(win, 0, sizeof(Window));

    zenith::strncpy(win->title, title, MAX_TITLE_LEN);
    win->frame = {x, y, w, h};
    win->state = WIN_NORMAL;
    win->z_order = idx;
    win->focused = true;
    win->dirty = true;
    win->dragging = false;
    win->resizing = false;
    win->saved_frame = win->frame;

    // Allocate content buffer
    Rect cr = win->content_rect();
    win->content_w = cr.w;
    win->content_h = cr.h;
    int buf_size = cr.w * cr.h * 4;
    win->content = (uint32_t*)zenith::alloc(buf_size);
    zenith::memset(win->content, 0xFF, buf_size);

    win->on_draw = nullptr;
    win->on_mouse = nullptr;
    win->on_key = nullptr;
    win->on_close = nullptr;
    win->on_poll = nullptr;
    win->app_data = nullptr;

    // Unfocus previous window
    if (ds->focused_window >= 0 && ds->focused_window < ds->window_count) {
        ds->windows[ds->focused_window].focused = false;
    }
    ds->focused_window = idx;
    ds->window_count++;

    return idx;
}

void gui::desktop_close_window(DesktopState* ds, int idx) {
    if (idx < 0 || idx >= ds->window_count) return;

    Window* win = &ds->windows[idx];
    if (win->on_close) win->on_close(win);

    // Free content buffer
    if (win->content) {
        zenith::free(win->content);
        win->content = nullptr;
    }

    // Shift remaining windows down
    for (int i = idx; i < ds->window_count - 1; i++) {
        ds->windows[i] = ds->windows[i + 1];
    }
    ds->window_count--;

    // Fix focused window index
    if (ds->focused_window == idx) {
        ds->focused_window = ds->window_count > 0 ? ds->window_count - 1 : -1;
    } else if (ds->focused_window > idx) {
        ds->focused_window--;
    }

    if (ds->focused_window >= 0 && ds->focused_window < ds->window_count) {
        ds->windows[ds->focused_window].focused = true;
    }
}

void gui::desktop_raise_window(DesktopState* ds, int idx) {
    if (idx < 0 || idx >= ds->window_count) return;
    if (idx == ds->window_count - 1) {
        // Already on top, just focus
        if (ds->focused_window >= 0 && ds->focused_window < ds->window_count)
            ds->windows[ds->focused_window].focused = false;
        ds->focused_window = idx;
        ds->windows[idx].focused = true;
        return;
    }

    // Unfocus current
    if (ds->focused_window >= 0 && ds->focused_window < ds->window_count) {
        ds->windows[ds->focused_window].focused = false;
    }

    // Move window to end (top of stack)
    Window tmp = ds->windows[idx];
    for (int i = idx; i < ds->window_count - 1; i++) {
        ds->windows[i] = ds->windows[i + 1];
    }
    ds->windows[ds->window_count - 1] = tmp;

    ds->focused_window = ds->window_count - 1;
    ds->windows[ds->focused_window].focused = true;
}

void gui::desktop_draw_window(DesktopState* ds, int idx) {
    if (idx < 0 || idx >= ds->window_count) return;
    Window* win = &ds->windows[idx];
    if (win->state == WIN_MINIMIZED || win->state == WIN_CLOSED) return;

    Framebuffer& fb = ds->fb;
    int x = win->frame.x;
    int y = win->frame.y;
    int w = win->frame.w;
    int h = win->frame.h;

    // Draw shadow
    draw_shadow(fb, x, y, w, h, SHADOW_SIZE, colors::SHADOW);

    // Draw window body
    fb.fill_rect(x, y, w, h, colors::WINDOW_BG);

    // Draw titlebar
    Color tb_bg = win->focused ? colors::TITLEBAR_BG : Color::from_rgb(0xE8, 0xE8, 0xE8);
    fb.fill_rect(x, y, w, TITLEBAR_HEIGHT, tb_bg);

    // Draw border
    draw_rect(fb, x, y, w, h, colors::BORDER);
    // Titlebar bottom separator
    draw_hline(fb, x, y + TITLEBAR_HEIGHT - 1, w, colors::BORDER);

    // Draw window buttons (macOS style: close, minimize, maximize)
    Rect close_r = win->close_btn_rect();
    Rect min_r = win->min_btn_rect();
    Rect max_r = win->max_btn_rect();

    fill_circle(fb, close_r.x + BTN_RADIUS, close_r.y + BTN_RADIUS, BTN_RADIUS, colors::CLOSE_BTN);
    fill_circle(fb, min_r.x + BTN_RADIUS, min_r.y + BTN_RADIUS, BTN_RADIUS, colors::MIN_BTN);
    fill_circle(fb, max_r.x + BTN_RADIUS, max_r.y + BTN_RADIUS, BTN_RADIUS, colors::MAX_BTN);

    // Draw title text centered in titlebar (after buttons)
    int title_x = x + 12 + 44 + BTN_RADIUS * 2 + 12; // after buttons
    int title_y = y + (TITLEBAR_HEIGHT - FONT_HEIGHT) / 2;
    int title_w = text_width(win->title);
    // Center in remaining space
    int remaining_w = w - (title_x - x) - 12;
    if (remaining_w > title_w) {
        title_x += (remaining_w - title_w) / 2;
    }
    draw_text(fb, title_x, title_y, win->title, colors::TEXT_COLOR);

    // Call app draw callback to render content (skip during resize — buffer is old size)
    if (win->on_draw && !win->resizing) {
        win->on_draw(win, fb);
    }

    // Blit content buffer to framebuffer (clip to actual buffer size during resize)
    Rect cr = win->content_rect();
    if (win->content) {
        int blit_w = cr.w < win->content_w ? cr.w : win->content_w;
        int blit_h = cr.h < win->content_h ? cr.h : win->content_h;
        fb.blit(cr.x, cr.y, blit_w, blit_h, win->content);
    }
}

void gui::desktop_draw_panel(DesktopState* ds) {
    Framebuffer& fb = ds->fb;
    int sw = ds->screen_w;

    // Panel gradient background (slightly lighter at top)
    for (int y = 0; y < PANEL_HEIGHT; y++) {
        int t = y * 255 / PANEL_HEIGHT;
        uint8_t r = colors::PANEL_BG.r + (10 - t * 10 / 255);
        uint8_t g = colors::PANEL_BG.g + (10 - t * 10 / 255);
        uint8_t b = colors::PANEL_BG.b + (10 - t * 10 / 255);
        fb.fill_rect(0, y, sw, 1, Color::from_rgb(r, g, b));
    }

    // Bottom highlight line
    fb.fill_rect(0, PANEL_HEIGHT - 1, sw, 1, Color::from_rgba(0xFF, 0xFF, 0xFF, 0x10));

    // App menu button (left side)
    int btn_x = 4;
    int btn_y = 2;
    int btn_w = 28;
    int btn_h = 28;

    if (ds->icon_appmenu.pixels) {
        int ix = btn_x + (btn_w - ds->icon_appmenu.width) / 2;
        int iy = btn_y + (btn_h - ds->icon_appmenu.height) / 2;
        fb.blit_alpha(ix, iy, ds->icon_appmenu.width, ds->icon_appmenu.height, ds->icon_appmenu.pixels);
    } else {
        for (int gr = 0; gr < 3; gr++) {
            for (int gc = 0; gc < 3; gc++) {
                int dx = btn_x + 6 + gc * 6;
                int dy = btn_y + 6 + gr * 6;
                fb.fill_rect(dx, dy, 3, 3, colors::PANEL_TEXT);
            }
        }
    }

    // Window indicator pills (center area)
    int indicator_x = 40;
    for (int i = 0; i < ds->window_count; i++) {
        Window* win = &ds->windows[i];
        if (win->state == WIN_CLOSED) continue;

        int tw = text_width(win->title);
        int pad = 12;
        int iw = tw + pad * 2;
        if (iw > 150) iw = 150;

        // Pre-blended indicator pill colors (opaque, blended against PANEL_BG)
        Color pill_bg = (i == ds->focused_window)
            ? colors::PANEL_INDICATOR_ACTIVE
            : colors::PANEL_INDICATOR_INACTIVE;

        fill_rounded_rect(fb, indicator_x, 4, iw, 24, 6, pill_bg);

        // Active window accent underline bar
        if (i == ds->focused_window) {
            fb.fill_rect(indicator_x + 4, 26, iw - 8, 2, colors::ACCENT);
        }

        // Truncate title if too long
        char short_title[20];
        zenith::strncpy(short_title, win->title, 18);

        int tx = indicator_x + pad;
        int ty = 4 + (24 - FONT_HEIGHT) / 2;
        draw_text(fb, tx, ty, short_title, colors::PANEL_TEXT);

        indicator_x += iw + 4;
    }

    // Date + Clock (right side)
    Zenith::DateTime dt;
    zenith::gettime(&dt);

    char clock_str[8];
    snprintf(clock_str, sizeof(clock_str), "%02d:%02d", (int)dt.Hour, (int)dt.Minute);
    int clock_w = text_width(clock_str);
    int clock_x = sw - clock_w - 12;
    int clock_y = (PANEL_HEIGHT - FONT_HEIGHT) / 2;
    draw_text(fb, clock_x, clock_y, clock_str, colors::PANEL_TEXT);

    // Date before clock
    char date_str[12];
    int month_idx = dt.Month > 0 && dt.Month <= 12 ? dt.Month - 1 : 0;
    snprintf(date_str, sizeof(date_str), "%s %d", month_names[month_idx], (int)dt.Day);
    int date_w = text_width(date_str);
    int date_x = clock_x - date_w - 16;
    draw_text(fb, date_x, clock_y, date_str, colors::PANEL_TEXT);

    // Network icon (to the left of the date)
    uint64_t now = zenith::get_milliseconds();
    if (now - ds->net_cfg_last_poll > 5000) {
        zenith::get_netcfg(&ds->cached_net_cfg);
        ds->net_cfg_last_poll = now;
    }

    int net_icon_x = date_x - 16 - 12;
    int net_icon_y = (PANEL_HEIGHT - 16) / 2;
    ds->net_icon_rect = {net_icon_x, net_icon_y, 16, 16};

    if (ds->icon_network.pixels) {
        if (ds->cached_net_cfg.ipAddress == 0) {
            uint32_t* src = ds->icon_network.pixels;
            int npx = 16 * 16;
            uint32_t tinted[256];
            for (int p = 0; p < npx; p++) {
                uint32_t px = src[p];
                uint8_t a = (px >> 24) & 0xFF;
                tinted[p] = ((uint32_t)a << 24) | 0x004444CC;
            }
            fb.blit_alpha(net_icon_x, net_icon_y, 16, 16, tinted);
        } else {
            fb.blit_alpha(net_icon_x, net_icon_y, ds->icon_network.width, ds->icon_network.height, ds->icon_network.pixels);
        }
    }
}

// ============================================================================
// App Menu (5 items with separator and rounded corners)
// ============================================================================

static constexpr int MENU_ITEM_COUNT = 9;
static constexpr int MENU_W = 220;
static constexpr int MENU_ITEM_H = 40;

static void desktop_draw_app_menu(DesktopState* ds) {
    Framebuffer& fb = ds->fb;

    int menu_x = 4;
    int menu_y = PANEL_HEIGHT + 2;
    int menu_h = MENU_ITEM_H * MENU_ITEM_COUNT + 10; // +10 for padding + separator

    // Menu shadow
    draw_shadow(fb, menu_x, menu_y, MENU_W, menu_h, 4, colors::SHADOW);

    // Menu background with rounded corners
    fill_rounded_rect(fb, menu_x, menu_y, MENU_W, menu_h, 8, colors::MENU_BG);
    draw_rect(fb, menu_x, menu_y, MENU_W, menu_h, colors::BORDER);

    // Menu items
    struct MenuItem {
        const char* label;
        SvgIcon* icon;
    };
    MenuItem items[MENU_ITEM_COUNT] = {
        { "Terminal",     &ds->icon_terminal },
        { "Files",        &ds->icon_filemanager },
        { "System Info",  &ds->icon_sysinfo },
        { "Calculator",   &ds->icon_calculator },
        { "Text Editor",  &ds->icon_texteditor },
        { "Kernel Log",   &ds->icon_terminal },
        { "Wikipedia",    &ds->icon_wikipedia },
        { "About",        &ds->icon_settings },
        { "Reboot",       &ds->icon_reboot },
    };

    int mx = ds->mouse.x;
    int my = ds->mouse.y;

    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        int iy = menu_y + 4 + i * MENU_ITEM_H;

        // Thin separator lines before utility apps and before Settings
        if (i == 3 || i == 7) {
            int sep_y = iy - 1;
            for (int sx = menu_x + 8; sx < menu_x + MENU_W - 8; sx++)
                fb.put_pixel(sx, sep_y, colors::BORDER);
            iy += 1;
        }

        Rect item_rect = {menu_x + 4, iy, MENU_W - 8, MENU_ITEM_H};

        // Hover highlight
        if (item_rect.contains(mx, my)) {
            fill_rounded_rect(fb, item_rect.x, item_rect.y, item_rect.w, item_rect.h, 4, colors::MENU_HOVER);
        }

        // Icon
        int icon_x = item_rect.x + 8;
        int icon_y = item_rect.y + (MENU_ITEM_H - 20) / 2;
        if (items[i].icon && items[i].icon->pixels) {
            fb.blit_alpha(icon_x, icon_y, items[i].icon->width, items[i].icon->height, items[i].icon->pixels);
        }

        // Label
        int tx = icon_x + 28;
        int ty = item_rect.y + (MENU_ITEM_H - FONT_HEIGHT) / 2;
        draw_text(fb, tx, ty, items[i].label, colors::TEXT_COLOR);
    }
}

static void desktop_draw_net_popup(DesktopState* ds) {
    Framebuffer& fb = ds->fb;

    int popup_w = 220;
    int popup_h = 130;
    int popup_x = ds->net_icon_rect.x + ds->net_icon_rect.w - popup_w;
    int popup_y = PANEL_HEIGHT + 2;
    if (popup_x < 4) popup_x = 4;

    draw_shadow(fb, popup_x, popup_y, popup_w, popup_h, 4, colors::SHADOW);
    fb.fill_rect(popup_x, popup_y, popup_w, popup_h, colors::MENU_BG);
    draw_rect(fb, popup_x, popup_y, popup_w, popup_h, colors::BORDER);

    int tx = popup_x + 12;
    int ty = popup_y + 10;
    int line_h = FONT_HEIGHT + 6;
    char line[64];

    Zenith::NetCfg& nc = ds->cached_net_cfg;

    if (nc.ipAddress != 0) {
        char ipbuf[20];
        format_ip(ipbuf, nc.ipAddress);
        snprintf(line, sizeof(line), "IP:      %s", ipbuf);
    } else {
        snprintf(line, sizeof(line), "IP:      Not connected");
    }
    draw_text(fb, tx, ty, line, colors::TEXT_COLOR);
    ty += line_h;

    char buf[20];
    format_ip(buf, nc.subnetMask);
    snprintf(line, sizeof(line), "Subnet:  %s", buf);
    draw_text(fb, tx, ty, line, colors::TEXT_COLOR);
    ty += line_h;

    format_ip(buf, nc.gateway);
    snprintf(line, sizeof(line), "Gateway: %s", buf);
    draw_text(fb, tx, ty, line, colors::TEXT_COLOR);
    ty += line_h;

    format_ip(buf, nc.dnsServer);
    snprintf(line, sizeof(line), "DNS:     %s", buf);
    draw_text(fb, tx, ty, line, colors::TEXT_COLOR);
    ty += line_h;

    format_mac(buf, nc.macAddress);
    snprintf(line, sizeof(line), "MAC:     %s", buf);
    draw_text(fb, tx, ty, line, colors::TEXT_COLOR);
}

// ============================================================================
// Reboot Dialog
// ============================================================================

struct RebootDialogState {
    DesktopState* ds;
    // Button layout (pixel-buffer-relative coordinates)
    int btn_w, btn_h, btn_y, reboot_x, cancel_x;
    bool hover_reboot, hover_cancel;
};

static void reboot_dialog_on_draw(Window* win, Framebuffer& fb) {
    RebootDialogState* rs = (RebootDialogState*)win->app_data;
    if (!rs) return;

    Canvas c(win);
    c.fill(colors::WINDOW_BG);

    // "Reboot the system?" centered
    const char* msg = "Reboot the system?";
    int tw = text_width(msg);
    c.text((c.w - tw) / 2, 30, msg, colors::TEXT_COLOR);

    // Compute button layout
    int btn_w = 100;
    int btn_h = 32;
    int btn_y = c.h - btn_h - 20;
    int gap = 20;
    int total_w = btn_w * 2 + gap;
    int bx = (c.w - total_w) / 2;
    rs->btn_w = btn_w;
    rs->btn_h = btn_h;
    rs->btn_y = btn_y;
    rs->reboot_x = bx;
    rs->cancel_x = bx + btn_w + gap;

    // Draw Reboot button
    Color reboot_bg = rs->hover_reboot
        ? Color::from_rgb(0xDD, 0x44, 0x44)
        : Color::from_rgb(0xCC, 0x33, 0x33);
    c.button(rs->reboot_x, btn_y, btn_w, btn_h, "Reboot", reboot_bg, colors::WHITE, 4);

    // Draw Cancel button
    Color cancel_bg = rs->hover_cancel
        ? Color::from_rgb(0x99, 0x99, 0x99)
        : Color::from_rgb(0x88, 0x88, 0x88);
    c.button(rs->cancel_x, btn_y, btn_w, btn_h, "Cancel", cancel_bg, colors::WHITE, 4);
}

static void reboot_dialog_on_mouse(Window* win, MouseEvent& ev) {
    RebootDialogState* rs = (RebootDialogState*)win->app_data;
    if (!rs) return;

    Rect cr = win->content_rect();
    int lx = ev.x - cr.x;
    int ly = ev.y - cr.y;

    Rect rb = {rs->reboot_x, rs->btn_y, rs->btn_w, rs->btn_h};
    Rect cb = {rs->cancel_x, rs->btn_y, rs->btn_w, rs->btn_h};
    rs->hover_reboot = rb.contains(lx, ly);
    rs->hover_cancel = cb.contains(lx, ly);

    if (ev.left_pressed()) {
        if (rs->hover_reboot) zenith::reset();
        if (rs->hover_cancel) {
            for (int i = 0; i < rs->ds->window_count; i++) {
                if (rs->ds->windows[i].app_data == rs) {
                    desktop_close_window(rs->ds, i);
                    return;
                }
            }
        }
    }
}

static void reboot_dialog_on_key(Window* win, const Zenith::KeyEvent& key) {
    RebootDialogState* rs = (RebootDialogState*)win->app_data;
    if (!rs || !key.pressed) return;

    if (key.ascii == '\n' || key.ascii == '\r') {
        zenith::reset();
    }
    if (key.scancode == 0x01) { // Escape
        for (int i = 0; i < rs->ds->window_count; i++) {
            if (rs->ds->windows[i].app_data == rs) {
                desktop_close_window(rs->ds, i);
                return;
            }
        }
    }
}

static void reboot_dialog_on_close(Window* win) {
    if (win->app_data) {
        zenith::mfree(win->app_data);
        win->app_data = nullptr;
    }
}

void open_reboot_dialog(DesktopState* ds) {
    int wx = (ds->screen_w - 300) / 2;
    int wy = (ds->screen_h - 150) / 2;
    int idx = desktop_create_window(ds, "Reboot", wx, wy, 300, 150);
    if (idx < 0) return;

    Window* win = &ds->windows[idx];
    RebootDialogState* rs = (RebootDialogState*)zenith::malloc(sizeof(RebootDialogState));
    zenith::memset(rs, 0, sizeof(RebootDialogState));
    rs->ds = ds;

    win->app_data = rs;
    win->on_draw = reboot_dialog_on_draw;
    win->on_mouse = reboot_dialog_on_mouse;
    win->on_key = reboot_dialog_on_key;
    win->on_close = reboot_dialog_on_close;
}

static gui::ResizeEdge hit_test_resize_edge(const gui::Rect& f, int mx, int my) {
    using namespace gui;
    int G = RESIZE_GRAB;
    if (!f.contains(mx, my)) return RESIZE_NONE;

    bool near_left   = mx < f.x + G;
    bool near_right  = mx >= f.x + f.w - G;
    bool near_top    = my < f.y + G;
    bool near_bottom = my >= f.y + f.h - G;

    // Corners first
    if (near_top && near_left)     return RESIZE_TOP_LEFT;
    if (near_top && near_right)    return RESIZE_TOP_RIGHT;
    if (near_bottom && near_left)  return RESIZE_BOTTOM_LEFT;
    if (near_bottom && near_right) return RESIZE_BOTTOM_RIGHT;

    // Edges
    if (near_top)    return RESIZE_TOP;
    if (near_bottom) return RESIZE_BOTTOM;
    if (near_left)   return RESIZE_LEFT;
    if (near_right)  return RESIZE_RIGHT;

    return RESIZE_NONE;
}

static gui::CursorStyle cursor_for_edge(gui::ResizeEdge edge) {
    using namespace gui;
    switch (edge) {
    case RESIZE_LEFT: case RESIZE_RIGHT:
        return CURSOR_RESIZE_H;
    case RESIZE_TOP: case RESIZE_BOTTOM:
        return CURSOR_RESIZE_V;
    case RESIZE_TOP_LEFT: case RESIZE_BOTTOM_RIGHT:
        return CURSOR_RESIZE_NWSE;
    case RESIZE_TOP_RIGHT: case RESIZE_BOTTOM_LEFT:
        return CURSOR_RESIZE_NESW;
    default:
        return CURSOR_ARROW;
    }
}

void gui::desktop_compose(DesktopState* ds) {
    Framebuffer& fb = ds->fb;

    // Desktop background gradient
    int sw = ds->screen_w;
    int sh = ds->screen_h;
    int grad_start = PANEL_HEIGHT;
    int grad_range = sh - grad_start;
    if (grad_range < 1) grad_range = 1;

    uint32_t* buf = fb.buffer();
    int pitch = fb.pitch();

    for (int y = 0; y < sh; y++) {
        uint32_t* row = (uint32_t*)((uint8_t*)buf + y * pitch);
        if (y < grad_start) {
            // Panel area - will be overwritten by panel drawing
            uint32_t px = colors::PANEL_BG.to_pixel();
            for (int x = 0; x < sw; x++) row[x] = px;
        } else {
            int t = y - grad_start;
            uint8_t r = 0xD0 - (0xD0 - 0xA0) * t / grad_range;
            uint8_t g = 0xD8 - (0xD8 - 0xA8) * t / grad_range;
            uint8_t b = 0xE8 - (0xE8 - 0xB8) * t / grad_range;
            uint32_t px = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            for (int x = 0; x < sw; x++) row[x] = px;
        }
    }

    // Draw windows from bottom to top
    for (int i = 0; i < ds->window_count; i++) {
        if (ds->windows[i].state != WIN_MINIMIZED && ds->windows[i].state != WIN_CLOSED) {
            desktop_draw_window(ds, i);
        }
    }

    // Draw panel on top
    desktop_draw_panel(ds);

    // Draw app menu if open
    if (ds->app_menu_open) {
        desktop_draw_app_menu(ds);
    }

    // Draw network popup if open
    if (ds->net_popup_open) {
        desktop_draw_net_popup(ds);
    }

    // Draw right-click context menu if open
    if (ds->ctx_menu_open) {
        static constexpr int CTX_MENU_W = 180;
        static constexpr int CTX_ITEM_H = 36;
        static constexpr int CTX_ITEM_COUNT = 4;
        int cmx = ds->ctx_menu_x;
        int cmy = ds->ctx_menu_y;
        int cmh = CTX_ITEM_H * CTX_ITEM_COUNT + 8;

        // Clamp to screen
        if (cmx + CTX_MENU_W > sw) cmx = sw - CTX_MENU_W;
        if (cmy + cmh > sh) cmy = sh - cmh;

        draw_shadow(fb, cmx, cmy, CTX_MENU_W, cmh, 4, colors::SHADOW);
        fill_rounded_rect(fb, cmx, cmy, CTX_MENU_W, cmh, 8, colors::MENU_BG);
        draw_rect(fb, cmx, cmy, CTX_MENU_W, cmh, colors::BORDER);

        struct CtxItem { const char* label; SvgIcon* icon; };
        CtxItem ctx_items[CTX_ITEM_COUNT] = {
            { "Terminal", &ds->icon_terminal },
            { "Files",    &ds->icon_filemanager },
            { "About",    &ds->icon_settings },
            { "Reboot",   &ds->icon_reboot },
        };

        int mmx = ds->mouse.x;
        int mmy = ds->mouse.y;

        for (int i = 0; i < CTX_ITEM_COUNT; i++) {
            int iy = cmy + 4 + i * CTX_ITEM_H;
            Rect item_r = {cmx + 4, iy, CTX_MENU_W - 8, CTX_ITEM_H};

            if (item_r.contains(mmx, mmy)) {
                fill_rounded_rect(fb, item_r.x, item_r.y, item_r.w, item_r.h, 4, colors::MENU_HOVER);
            }

            int icon_x = item_r.x + 8;
            int icon_y = item_r.y + (CTX_ITEM_H - 20) / 2;
            if (ctx_items[i].icon && ctx_items[i].icon->pixels) {
                fb.blit_alpha(icon_x, icon_y, ctx_items[i].icon->width, ctx_items[i].icon->height, ctx_items[i].icon->pixels);
            }

            int tx = icon_x + 28;
            int ty = item_r.y + (CTX_ITEM_H - FONT_HEIGHT) / 2;
            draw_text(fb, tx, ty, ctx_items[i].label, colors::TEXT_COLOR);
        }
    }

    // Draw snap preview overlay while dragging to screen edge
    for (int i = 0; i < ds->window_count; i++) {
        Window* win = &ds->windows[i];
        if (win->dragging) {
            int dmx = ds->mouse.x;
            if (dmx <= 0) {
                fb.fill_rect_alpha(0, PANEL_HEIGHT, sw / 2, sh - PANEL_HEIGHT,
                    Color::from_rgba(0x33, 0x77, 0xCC, 0x30));
            } else if (dmx >= sw - 1) {
                fb.fill_rect_alpha(sw / 2, PANEL_HEIGHT, sw / 2, sh - PANEL_HEIGHT,
                    Color::from_rgba(0x33, 0x77, 0xCC, 0x30));
            }
            break;
        }
    }

    // Determine cursor style based on resize hover or active resize
    CursorStyle cur_style = CURSOR_ARROW;
    for (int i = ds->window_count - 1; i >= 0; i--) {
        Window* win = &ds->windows[i];
        if (win->resizing) {
            cur_style = cursor_for_edge(win->resize_edge);
            break;
        }
        if (win->state == WIN_MINIMIZED || win->state == WIN_CLOSED || win->state == WIN_MAXIMIZED)
            continue;
        if (win->frame.contains(ds->mouse.x, ds->mouse.y)) {
            ResizeEdge edge = hit_test_resize_edge(win->frame, ds->mouse.x, ds->mouse.y);
            if (edge != RESIZE_NONE) {
                cur_style = cursor_for_edge(edge);
            }
            break;
        }
    }

    // Draw cursor last
    draw_cursor(fb, ds->mouse.x, ds->mouse.y, cur_style);
}

void gui::desktop_handle_mouse(DesktopState* ds) {
    int mx = ds->mouse.x;
    int my = ds->mouse.y;
    uint8_t buttons = ds->mouse.buttons;
    uint8_t prev = ds->prev_buttons;
    bool left_pressed = (buttons & 0x01) && !(prev & 0x01);
    bool left_held = (buttons & 0x01);
    bool left_released = !(buttons & 0x01) && (prev & 0x01);
    bool right_pressed = (buttons & 0x02) && !(prev & 0x02);

    MouseEvent ev;
    ev.x = mx;
    ev.y = my;
    ev.buttons = buttons;
    ev.prev_buttons = prev;
    ev.scroll = ds->mouse.scrollDelta;

    // Handle context menu clicks
    if (ds->ctx_menu_open) {
        if (left_pressed) {
            static constexpr int CTX_MENU_W = 180;
            static constexpr int CTX_ITEM_H = 36;
            static constexpr int CTX_ITEM_COUNT = 4;
            int cmx = ds->ctx_menu_x;
            int cmy = ds->ctx_menu_y;
            int cmh = CTX_ITEM_H * CTX_ITEM_COUNT + 8;
            if (cmx + CTX_MENU_W > ds->screen_w) cmx = ds->screen_w - CTX_MENU_W;
            if (cmy + cmh > ds->screen_h) cmy = ds->screen_h - cmh;

            Rect ctx_rect = {cmx, cmy, CTX_MENU_W, cmh};
            if (ctx_rect.contains(mx, my)) {
                int rel_y = my - cmy - 4;
                int item_idx = rel_y / CTX_ITEM_H;
                if (item_idx >= 0 && item_idx < CTX_ITEM_COUNT) {
                    ds->ctx_menu_open = false;
                    switch (item_idx) {
                    case 0: open_terminal(ds); break;
                    case 1: open_filemanager(ds); break;
                    case 2: open_settings(ds); break;
                    case 3: open_reboot_dialog(ds); break;
                    }
                    return;
                }
            }
            ds->ctx_menu_open = false;
            return;
        }
        if (right_pressed) {
            ds->ctx_menu_open = false;
            return;
        }
    }

    // Check for ongoing window drags first
    for (int i = 0; i < ds->window_count; i++) {
        Window* win = &ds->windows[i];
        if (win->dragging) {
            if (left_held) {
                win->frame.x = mx - win->drag_offset_x;
                win->frame.y = my - win->drag_offset_y;
                if (win->frame.x < -win->frame.w + 50) win->frame.x = -win->frame.w + 50;
                if (win->frame.y < 0) win->frame.y = 0;
                if (win->frame.x > ds->screen_w - 50) win->frame.x = ds->screen_w - 50;
                if (win->frame.y > ds->screen_h - 50) win->frame.y = ds->screen_h - 50;
            }
            if (left_released) {
                win->dragging = false;
                // Window edge snapping
                if (mx <= 0) {
                    win->saved_frame = win->frame;
                    win->frame = {0, PANEL_HEIGHT, ds->screen_w / 2, ds->screen_h - PANEL_HEIGHT};
                    win->state = WIN_MAXIMIZED;
                    Rect cr = win->content_rect();
                    if (cr.w != win->content_w || cr.h != win->content_h) {
                        if (win->content) zenith::free(win->content);
                        win->content_w = cr.w;
                        win->content_h = cr.h;
                        win->content = (uint32_t*)zenith::alloc(cr.w * cr.h * 4);
                        zenith::memset(win->content, 0xFF, cr.w * cr.h * 4);
                    }
                } else if (mx >= ds->screen_w - 1) {
                    win->saved_frame = win->frame;
                    win->frame = {ds->screen_w / 2, PANEL_HEIGHT, ds->screen_w / 2, ds->screen_h - PANEL_HEIGHT};
                    win->state = WIN_MAXIMIZED;
                    Rect cr = win->content_rect();
                    if (cr.w != win->content_w || cr.h != win->content_h) {
                        if (win->content) zenith::free(win->content);
                        win->content_w = cr.w;
                        win->content_h = cr.h;
                        win->content = (uint32_t*)zenith::alloc(cr.w * cr.h * 4);
                        zenith::memset(win->content, 0xFF, cr.w * cr.h * 4);
                    }
                }
            }
            return;
        }
    }

    // Check for ongoing window resizes
    for (int i = 0; i < ds->window_count; i++) {
        Window* win = &ds->windows[i];
        if (win->resizing) {
            if (left_held) {
                int dx = mx - win->resize_start_mx;
                int dy = my - win->resize_start_my;
                Rect sf = win->resize_start_frame;
                ResizeEdge edge = win->resize_edge;

                int new_x = sf.x, new_y = sf.y, new_w = sf.w, new_h = sf.h;

                if (edge == RESIZE_RIGHT || edge == RESIZE_TOP_RIGHT || edge == RESIZE_BOTTOM_RIGHT)
                    new_w = sf.w + dx;
                if (edge == RESIZE_BOTTOM || edge == RESIZE_BOTTOM_LEFT || edge == RESIZE_BOTTOM_RIGHT)
                    new_h = sf.h + dy;
                if (edge == RESIZE_LEFT || edge == RESIZE_TOP_LEFT || edge == RESIZE_BOTTOM_LEFT) {
                    new_x = sf.x + dx;
                    new_w = sf.w - dx;
                }
                if (edge == RESIZE_TOP || edge == RESIZE_TOP_LEFT || edge == RESIZE_TOP_RIGHT) {
                    new_y = sf.y + dy;
                    new_h = sf.h - dy;
                }

                // Enforce minimum size
                if (new_w < MIN_WINDOW_W) {
                    if (edge == RESIZE_LEFT || edge == RESIZE_TOP_LEFT || edge == RESIZE_BOTTOM_LEFT)
                        new_x = sf.x + sf.w - MIN_WINDOW_W;
                    new_w = MIN_WINDOW_W;
                }
                if (new_h < MIN_WINDOW_H) {
                    if (edge == RESIZE_TOP || edge == RESIZE_TOP_LEFT || edge == RESIZE_TOP_RIGHT)
                        new_y = sf.y + sf.h - MIN_WINDOW_H;
                    new_h = MIN_WINDOW_H;
                }

                win->frame = {new_x, new_y, new_w, new_h};
            }
            if (left_released) {
                win->resizing = false;
                // Reallocate content buffer if dimensions changed
                Rect cr = win->content_rect();
                if (cr.w != win->content_w || cr.h != win->content_h) {
                    if (win->content) zenith::free(win->content);
                    win->content_w = cr.w;
                    win->content_h = cr.h;
                    win->content = (uint32_t*)zenith::alloc(cr.w * cr.h * 4);
                    zenith::memset(win->content, 0xFF, cr.w * cr.h * 4);
                }
                win->dirty = true;
            }
            return;
        }
    }

    // Handle app menu clicks
    if (ds->app_menu_open && left_pressed) {
        int menu_x = 4;
        int menu_y = PANEL_HEIGHT + 2;
        int menu_h = MENU_ITEM_H * MENU_ITEM_COUNT + 10;
        Rect menu_rect = {menu_x, menu_y, MENU_W, menu_h};

        if (menu_rect.contains(mx, my)) {
            int rel_y = my - menu_y - 4;
            int item_idx = rel_y / MENU_ITEM_H;
            if (item_idx >= 0 && item_idx < MENU_ITEM_COUNT) {
                switch (item_idx) {
                case 0: open_terminal(ds); break;
                case 1: open_filemanager(ds); break;
                case 2: open_sysinfo(ds); break;
                case 3: open_calculator(ds); break;
                case 4: open_texteditor(ds); break;
                case 5: open_klog(ds); break;
                case 6: open_wiki(ds); break;
                case 7: open_settings(ds); break;
                case 8: open_reboot_dialog(ds); break;
                }
                ds->app_menu_open = false;
            }
            return;
        } else {
            ds->app_menu_open = false;
        }
    }

    // Handle net popup clicks
    if (ds->net_popup_open && left_pressed) {
        int popup_w = 220;
        int popup_h = 130;
        int popup_x = ds->net_icon_rect.x + ds->net_icon_rect.w - popup_w;
        int popup_y = PANEL_HEIGHT + 2;
        if (popup_x < 4) popup_x = 4;
        Rect popup_rect = {popup_x, popup_y, popup_w, popup_h};

        if (popup_rect.contains(mx, my)) {
            return;
        } else if (!ds->net_icon_rect.contains(mx, my)) {
            ds->net_popup_open = false;
        }
    }

    // Panel click check
    if (left_pressed && my < PANEL_HEIGHT) {
        // App menu button
        if (mx < 36) {
            ds->app_menu_open = !ds->app_menu_open;
            ds->net_popup_open = false;
            ds->ctx_menu_open = false;
            return;
        }

        // Network icon
        if (ds->net_icon_rect.w > 0 && ds->net_icon_rect.contains(mx, my)) {
            ds->net_popup_open = !ds->net_popup_open;
            ds->app_menu_open = false;
            ds->ctx_menu_open = false;
            return;
        }
        // Window indicator buttons
        int indicator_x = 40;
        for (int i = 0; i < ds->window_count; i++) {
            Window* win = &ds->windows[i];
            if (win->state == WIN_CLOSED) continue;

            int tw = text_width(win->title);
            int pad = 12;
            int iw = tw + pad * 2;
            if (iw > 150) iw = 150;

            Rect btn_rect = {indicator_x, 4, iw, 24};
            if (btn_rect.contains(mx, my)) {
                if (win->state == WIN_MINIMIZED) {
                    win->state = WIN_NORMAL;
                }
                desktop_raise_window(ds, i);
                return;
            }
            indicator_x += iw + 4;
        }
        return;
    }

    // Window interaction: check from top (last) to bottom (first)
    if (left_pressed) {
        for (int i = ds->window_count - 1; i >= 0; i--) {
            Window* win = &ds->windows[i];
            if (win->state == WIN_MINIMIZED || win->state == WIN_CLOSED) continue;

            // Check close button
            Rect close_r = win->close_btn_rect();
            if (close_r.contains(mx, my)) {
                desktop_close_window(ds, i);
                return;
            }

            // Check minimize button
            Rect min_r = win->min_btn_rect();
            if (min_r.contains(mx, my)) {
                win->state = WIN_MINIMIZED;
                if (ds->focused_window == i) {
                    ds->focused_window = -1;
                    for (int j = ds->window_count - 1; j >= 0; j--) {
                        if (ds->windows[j].state == WIN_NORMAL || ds->windows[j].state == WIN_MAXIMIZED) {
                            ds->focused_window = j;
                            ds->windows[j].focused = true;
                            break;
                        }
                    }
                }
                return;
            }

            // Check maximize button
            Rect max_r = win->max_btn_rect();
            if (max_r.contains(mx, my)) {
                if (win->state == WIN_MAXIMIZED) {
                    win->frame = win->saved_frame;
                    win->state = WIN_NORMAL;
                } else {
                    win->saved_frame = win->frame;
                    win->frame = {0, PANEL_HEIGHT, ds->screen_w, ds->screen_h - PANEL_HEIGHT};
                    win->state = WIN_MAXIMIZED;
                }
                Rect cr = win->content_rect();
                if (cr.w != win->content_w || cr.h != win->content_h) {
                    if (win->content) zenith::free(win->content);
                    win->content_w = cr.w;
                    win->content_h = cr.h;
                    win->content = (uint32_t*)zenith::alloc(cr.w * cr.h * 4);
                    zenith::memset(win->content, 0xFF, cr.w * cr.h * 4);
                }
                desktop_raise_window(ds, i);
                return;
            }

            // Check resize edges (before titlebar drag, so corner grabs work)
            if (win->state != WIN_MAXIMIZED) {
                ResizeEdge edge = hit_test_resize_edge(win->frame, mx, my);
                if (edge != RESIZE_NONE) {
                    win->resizing = true;
                    win->resize_edge = edge;
                    win->resize_start_frame = win->frame;
                    win->resize_start_mx = mx;
                    win->resize_start_my = my;
                    desktop_raise_window(ds, i);
                    int new_idx = ds->window_count - 1;
                    ds->windows[new_idx].resizing = true;
                    ds->windows[new_idx].resize_edge = edge;
                    ds->windows[new_idx].resize_start_frame = ds->windows[new_idx].frame;
                    ds->windows[new_idx].resize_start_mx = mx;
                    ds->windows[new_idx].resize_start_my = my;
                    return;
                }
            }

            // Check titlebar (start drag)
            Rect tb = win->titlebar_rect();
            if (tb.contains(mx, my)) {
                win->dragging = true;
                win->drag_offset_x = mx - win->frame.x;
                win->drag_offset_y = my - win->frame.y;
                desktop_raise_window(ds, i);
                int new_idx = ds->window_count - 1;
                ds->windows[new_idx].dragging = true;
                ds->windows[new_idx].drag_offset_x = mx - ds->windows[new_idx].frame.x;
                ds->windows[new_idx].drag_offset_y = my - ds->windows[new_idx].frame.y;
                return;
            }

            // Check content area
            Rect cr = win->content_rect();
            if (cr.contains(mx, my)) {
                desktop_raise_window(ds, i);
                int new_idx = ds->window_count - 1;
                if (ds->windows[new_idx].on_mouse) {
                    ev.x = mx;
                    ev.y = my;
                    ds->windows[new_idx].on_mouse(&ds->windows[new_idx], ev);
                }
                return;
            }

            // Check full frame
            if (win->frame.contains(mx, my)) {
                desktop_raise_window(ds, i);
                return;
            }
        }

        ds->app_menu_open = false;
        ds->ctx_menu_open = false;
    }

    // Handle scroll events on focused window
    if (ev.scroll != 0 && ds->focused_window >= 0) {
        Window* win = &ds->windows[ds->focused_window];
        Rect cr = win->content_rect();
        if (cr.contains(mx, my) && win->on_mouse) {
            win->on_mouse(win, ev);
        }
    }

    // Right-click on desktop background opens context menu
    if (right_pressed && my >= PANEL_HEIGHT) {
        bool on_window = false;
        for (int i = ds->window_count - 1; i >= 0; i--) {
            Window* win = &ds->windows[i];
            if (win->state == WIN_MINIMIZED || win->state == WIN_CLOSED) continue;
            if (win->frame.contains(mx, my)) {
                on_window = true;
                break;
            }
        }
        if (!on_window) {
            ds->ctx_menu_open = true;
            ds->ctx_menu_x = mx;
            ds->ctx_menu_y = my;
            ds->app_menu_open = false;
            ds->net_popup_open = false;
        }
    }
}

void gui::desktop_handle_keyboard(DesktopState* ds, const Zenith::KeyEvent& key) {
    if (!key.pressed) return;

    // Global shortcuts
    if (key.ctrl && key.alt) {
        if (key.ascii == 't' || key.ascii == 'T') {
            open_terminal(ds);
            return;
        }
        if (key.ascii == 'f' || key.ascii == 'F') {
            open_filemanager(ds);
            return;
        }
        if (key.ascii == 'i' || key.ascii == 'I') {
            open_sysinfo(ds);
            return;
        }
        if (key.ascii == 'c' || key.ascii == 'C') {
            open_calculator(ds);
            return;
        }
        if (key.ascii == 'e' || key.ascii == 'E') {
            open_texteditor(ds);
            return;
        }
        if (key.ascii == 'k' || key.ascii == 'K') {
            open_klog(ds);
            return;
        }
    }

    // Dispatch to focused window
    if (ds->focused_window >= 0 && ds->focused_window < ds->window_count) {
        Window* win = &ds->windows[ds->focused_window];
        if (win->on_key) {
            win->on_key(win, key);
        }
    }
}

void gui::desktop_run(DesktopState* ds) {
    for (;;) {
        // Poll mouse state
        ds->prev_buttons = ds->mouse.buttons;
        zenith::mouse_state(&ds->mouse);

        // Poll keyboard events
        while (zenith::is_key_available()) {
            Zenith::KeyEvent key;
            zenith::getkey(&key);
            desktop_handle_keyboard(ds, key);
        }

        // Poll windows that have a poll callback
        for (int i = 0; i < ds->window_count; i++) {
            Window* win = &ds->windows[i];
            if (win->state == WIN_CLOSED) continue;
            if (win->on_poll) {
                win->on_poll(win);
            }
        }

        // Handle mouse events
        desktop_handle_mouse(ds);

        // Compose and present
        desktop_compose(ds);
        ds->fb.flip();

        // Target ~60fps
        zenith::sleep_ms(16);
    }
}

// ============================================================================
// Entry Point
// ============================================================================

static DesktopState* g_desktop;

extern "C" void _start() {
    DesktopState* ds = (DesktopState*)zenith::malloc(sizeof(DesktopState));
    zenith::memset(ds, 0, sizeof(DesktopState));

    // Placement-new the Framebuffer since it has a constructor
    new (&ds->fb) Framebuffer();

    g_desktop = ds;

    desktop_init(ds);
    desktop_run(ds);

    zenith::exit(0);
}
