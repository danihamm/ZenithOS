/*
 * devexplorer.h
 * Shared header for the MontaukOS Device Explorer
 * Copyright (c) 2026 Daniel Hammer
 */

#pragma once

#include <montauk/syscall.h>
#include <montauk/string.h>
#include <montauk/heap.h>
#include <gui/gui.hpp>
#include <gui/truetype.hpp>

extern "C" {
#include <string.h>
#include <stdio.h>
}

using namespace gui;

// ============================================================================
// Constants
// ============================================================================

static constexpr int INIT_W       = 640;
static constexpr int INIT_H       = 460;
static constexpr int TOOLBAR_H    = 36;
static constexpr int CAT_H        = 28;
static constexpr int ITEM_H       = 24;
static constexpr int MAX_DEVS     = 64;
static constexpr int POLL_MS      = 2000;
static constexpr int INDENT       = 28;
static constexpr int FONT_SIZE    = 18;

static constexpr int NUM_CATEGORIES = 9;

static constexpr Color BG_COLOR       = Color::from_rgb(0xFF, 0xFF, 0xFF);
static constexpr Color TOOLBAR_BG     = Color::from_rgb(0xF5, 0xF5, 0xF5);
static constexpr Color BORDER_COLOR   = Color::from_rgb(0xCC, 0xCC, 0xCC);
static constexpr Color TEXT_COLOR     = Color::from_rgb(0x22, 0x22, 0x22);
static constexpr Color DIM_TEXT       = Color::from_rgb(0x66, 0x66, 0x66);
static constexpr Color WHITE          = Color::from_rgb(0xFF, 0xFF, 0xFF);
static constexpr Color ACCENT_COLOR   = Color::from_rgb(0x33, 0x66, 0xCC);

// ============================================================================
// Category data
// ============================================================================

static const char* category_names[] = {
    "CPU",          // 0
    "Interrupt",    // 1
    "Timer",        // 2
    "Input",        // 3
    "USB",          // 4
    "Network",      // 5
    "Display",      // 6
    "Storage",      // 7
    "PCI",          // 8
};

static const Color category_colors[] = {
    Color::from_rgb(0x33, 0x66, 0xCC),  // CPU - blue
    Color::from_rgb(0x88, 0x44, 0xAA),  // Interrupt - purple
    Color::from_rgb(0x22, 0x88, 0x22),  // Timer - green
    Color::from_rgb(0xCC, 0x88, 0x00),  // Input - amber
    Color::from_rgb(0x00, 0x88, 0x88),  // USB - teal
    Color::from_rgb(0xCC, 0x55, 0x22),  // Network - orange
    Color::from_rgb(0x44, 0x66, 0xCC),  // Display - indigo
    Color::from_rgb(0x99, 0x55, 0x00),  // Storage - brown
    Color::from_rgb(0x66, 0x66, 0x66),  // PCI - gray
};

// ============================================================================
// Display row model
// ============================================================================

enum RowType { ROW_CATEGORY, ROW_DEVICE };

struct DisplayRow {
    RowType type;
    int category;
    int dev_index;
};

static constexpr int MAX_DISPLAY_ROWS = MAX_DEVS + NUM_CATEGORIES;

// ============================================================================
// Disk detail window
// ============================================================================

static constexpr int DD_TAB_COUNT = 2;
static const char* dd_tab_labels[DD_TAB_COUNT] = { "General", "Features" };

static constexpr int DD_INIT_W = 440;
static constexpr int DD_INIT_H = 420;
static constexpr int DD_TAB_BAR_H = 32;

struct DiskDetailState {
    Montauk::DiskInfo info;
    int active_tab;
    int win_id;
    int win_w, win_h;
    uint32_t* pixels;
    bool open;
};

// ============================================================================
// Main state
// ============================================================================

struct DevExplorerState {
    Montauk::DevInfo devs[MAX_DEVS];
    int dev_count;
    bool collapsed[NUM_CATEGORIES];
    int selected_row;
    int scroll_y;
    uint64_t last_poll_ms;

    int last_click_row;
    uint64_t last_click_ms;

    DiskDetailState detail;
};

// ============================================================================
// Global state (extern — defined in main.cpp)
// ============================================================================

extern int g_win_w, g_win_h;
extern DevExplorerState g_state;
extern TrueTypeFont* g_font;

// ============================================================================
// Function declarations — render.cpp
// ============================================================================

void render(uint32_t* pixels);

// ============================================================================
// Function declarations — main.cpp
// ============================================================================

int build_display_rows(DevExplorerState* de, DisplayRow* rows);

// ============================================================================
// Function declarations — diskdetail.cpp
// ============================================================================

void render_disk_detail();
void open_disk_detail(int port, const char* model);
void close_disk_detail();
bool handle_detail_mouse(int mx, int my, bool clicked);
