/*
    * desktop_internal.hpp
    * Shared declarations for desktop source files
    * Copyright (c) 2026 Daniel Hammer
*/

#pragma once

#include "apps/apps_common.hpp"
#include "wallpaper.hpp"

// ============================================================================
// App Menu Data
// ============================================================================

static constexpr int MENU_W = 220;
static constexpr int MENU_ITEM_H = 36;
static constexpr int MENU_CAT_H = 32;
static constexpr int MENU_DIV_H = 10;

struct MenuRow {
    bool is_category;
    const char* label;    // "" for divider-only rows
    int app_id;           // -1 for category headers / dividers
};

static constexpr int MENU_ROW_COUNT = 23;
static const MenuRow menu_rows[MENU_ROW_COUNT] = {
    { true,  "Applications", -1 },   // cat 0
    { false, "Terminal",      0 },
    { false, "Files",         1 },
    { false, "Text Editor",   4 },
    { false, "Word Processor", 15 },
    { false, "Spreadsheet",  16 },
    { false, "Calculator",    3 },
    { true,  "Internet",     -1 },    // cat 1
    { false, "Wikipedia",     9 },
    { false, "Weather",      13 },
    { true,  "System",       -1 },    // cat 2
    { false, "System Info",   2 },
    { false, "Kernel Log",    5 },
    { false, "Processes",     6 },
    { false, "Devices",       8 },
    { false, "Disks",        17 },
    { true,  "Games",        -1 },    // cat 3
    { false, "Mandelbrot",    7 },
    { false, "DOOM",         10 },
    { true,  "",             -1 },    // divider (always visible)
    { false, "Settings",     11 },
    { false, "Reboot",       12 },
    { false, "Shutdown",     14 },
};

// Collapsible category state (categories 0-3 are toggleable; divider category always expanded)
static constexpr int MENU_NUM_CATS = 5;
inline bool menu_cat_expanded[MENU_NUM_CATS] = { false, false, false, false, true };

inline int menu_get_cat(int row_idx) {
    int cat = -1;
    for (int i = 0; i <= row_idx; i++)
        if (menu_rows[i].is_category) cat++;
    return cat;
}

inline bool menu_row_visible(int row_idx) {
    const MenuRow& row = menu_rows[row_idx];
    if (row.is_category) return true;
    int cat = menu_get_cat(row_idx);
    if (cat < 0 || cat >= MENU_NUM_CATS) return true;
    return menu_cat_expanded[cat];
}

inline int menu_row_height(const MenuRow& row) {
    if (!row.is_category) return MENU_ITEM_H;
    return row.label[0] ? MENU_CAT_H : MENU_DIV_H;
}

inline int menu_total_height() {
    int h = 10; // top + bottom padding
    for (int i = 0; i < MENU_ROW_COUNT; i++)
        if (menu_row_visible(i))
            h += menu_row_height(menu_rows[i]);
    return h;
}

// ============================================================================
// Forward Declarations (internal functions)
// ============================================================================

// window.cpp
gui::ResizeEdge hit_test_resize_edge(const gui::Rect& f, int mx, int my);
gui::CursorStyle cursor_for_edge(gui::ResizeEdge edge);

// panel.cpp
void desktop_draw_app_menu(gui::DesktopState* ds);
void desktop_draw_net_popup(gui::DesktopState* ds);

// Month names (shared by panel clock)
inline const char* month_names[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};
