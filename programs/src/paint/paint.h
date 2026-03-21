/*
 * paint.h
 * Shared header for the MontaukOS Paint app
 * Copyright (c) 2026 Daniel Hammer
 */

#pragma once

#include <montauk/syscall.h>
#include <montauk/string.h>
#include <montauk/heap.h>
#include <gui/gui.hpp>
#include <gui/truetype.hpp>
#include <gui/stb_math.h>

extern "C" {
#include <string.h>
#include <stdio.h>
}

using namespace gui;

// ============================================================================
// Constants
// ============================================================================

static constexpr int INIT_W        = 800;
static constexpr int INIT_H        = 600;
static constexpr int TOOLBAR_H     = 36;
static constexpr int TB_BTN_SIZE   = 24;
static constexpr int TB_BTN_Y      = 6;
static constexpr int TB_BTN_RAD    = 3;
static constexpr int STATUS_BAR_H  = 24;
static constexpr int COLOR_BAR_H   = 36;

static constexpr int SWATCH_SIZE   = 20;
static constexpr int SWATCH_PAD    = 4;
static constexpr int SWATCH_Y      = 8;

static constexpr int MAX_CANVAS_W  = 2048;
static constexpr int MAX_CANVAS_H  = 2048;

static constexpr int FONT_SIZE     = 16;

// UI colors
static constexpr Color BG_COLOR       = Color::from_rgb(0xE0, 0xE0, 0xE0);
static constexpr Color TOOLBAR_BG     = Color::from_rgb(0xF5, 0xF5, 0xF5);
static constexpr Color TB_BTN_BG      = Color::from_rgb(0xE8, 0xE8, 0xE8);
static constexpr Color TB_BTN_ACTIVE  = Color::from_rgb(0xC0, 0xD0, 0xE8);
static constexpr Color TB_SEP_COLOR   = Color::from_rgb(0xCC, 0xCC, 0xCC);
static constexpr Color HEADER_TEXT    = Color::from_rgb(0x55, 0x55, 0x55);
static constexpr Color STATUS_BG      = Color::from_rgb(0x2B, 0x3E, 0x50);
static constexpr Color STATUS_TEXT    = Color::from_rgb(0xFF, 0xFF, 0xFF);
static constexpr Color CANVAS_BORDER  = Color::from_rgb(0x99, 0x99, 0x99);
static constexpr Color SELECT_BORDER  = Color::from_rgb(0x36, 0x7B, 0xF0);

// ============================================================================
// Tool types
// ============================================================================

enum Tool : uint8_t {
    TOOL_PENCIL = 0,
    TOOL_BRUSH,
    TOOL_ERASER,
    TOOL_LINE,
    TOOL_RECT,
    TOOL_FILLED_RECT,
    TOOL_ELLIPSE,
    TOOL_FILLED_ELLIPSE,
    TOOL_FILL,
    TOOL_EYEDROPPER,
    TOOL_COUNT
};

// ============================================================================
// Palette colors
// ============================================================================

static constexpr int PALETTE_COUNT = 28;
static constexpr Color PALETTE[PALETTE_COUNT] = {
    // Row 1: basic colors
    Color::from_rgb(0x00, 0x00, 0x00), // Black
    Color::from_rgb(0x7F, 0x7F, 0x7F), // Gray
    Color::from_rgb(0x88, 0x00, 0x15), // Dark red
    Color::from_rgb(0xED, 0x1C, 0x24), // Red
    Color::from_rgb(0xFF, 0x7F, 0x27), // Orange
    Color::from_rgb(0xFF, 0xF2, 0x00), // Yellow
    Color::from_rgb(0x22, 0xB1, 0x4C), // Green
    Color::from_rgb(0x00, 0xA2, 0xE8), // Light blue
    Color::from_rgb(0x3F, 0x48, 0xCC), // Blue
    Color::from_rgb(0xA3, 0x49, 0xA4), // Purple
    Color::from_rgb(0xB9, 0x7A, 0x57), // Brown
    Color::from_rgb(0xFF, 0xAE, 0xC9), // Pink
    Color::from_rgb(0xB5, 0xE6, 0x1D), // Lime
    Color::from_rgb(0x99, 0xD9, 0xEA), // Sky
    // Row 2: lighter variants and white
    Color::from_rgb(0xFF, 0xFF, 0xFF), // White
    Color::from_rgb(0xC3, 0xC3, 0xC3), // Light gray
    Color::from_rgb(0xB9, 0x5B, 0x5B), // Salmon
    Color::from_rgb(0xFF, 0xC9, 0x0E), // Gold
    Color::from_rgb(0xFE, 0xF0, 0x82), // Light yellow
    Color::from_rgb(0xA8, 0xE6, 0x1D), // Bright lime
    Color::from_rgb(0x70, 0xDB, 0x93), // Sea green
    Color::from_rgb(0x7E, 0xC8, 0xE3), // Powder blue
    Color::from_rgb(0x00, 0x78, 0xD7), // Bright blue
    Color::from_rgb(0xC8, 0xBF, 0xE7), // Lavender
    Color::from_rgb(0xD9, 0x9E, 0x82), // Tan
    Color::from_rgb(0xDE, 0xCE, 0xEF), // Light lavender
    Color::from_rgb(0xE0, 0xE0, 0xD0), // Cream
    Color::from_rgb(0xF0, 0xD0, 0xB0), // Peach
};

// ============================================================================
// Brush sizes
// ============================================================================

static constexpr int BRUSH_SIZES[] = { 1, 2, 4, 8, 12, 20 };
static constexpr int BRUSH_SIZE_COUNT = 6;

// ============================================================================
// Global state (extern -- defined in main.cpp)
// ============================================================================

extern int g_win_w, g_win_h;

// Canvas
extern uint32_t* g_canvas;
extern int g_canvas_w, g_canvas_h;

// Undo
static constexpr int UNDO_MAX = 16;
extern uint32_t* g_undo_buf[UNDO_MAX];
extern int g_undo_count;
extern int g_undo_pos;

// View
extern int g_scroll_x, g_scroll_y;

// Tool state
extern Tool g_tool;
extern Color g_fg_color;
extern Color g_bg_color;
extern int g_brush_idx;

// Drawing state
extern bool g_drawing;
extern int g_draw_x0, g_draw_y0;
extern int g_draw_x1, g_draw_y1;
extern int g_prev_x, g_prev_y;

// Font
extern TrueTypeFont* g_font;

// Modified flag
extern bool g_modified;
extern char g_filepath[256];

// ============================================================================
// Function declarations -- helpers.cpp
// ============================================================================

void px_fill(uint32_t* px, int bw, int bh, int x, int y, int w, int h, Color c);
void px_hline(uint32_t* px, int bw, int bh, int x, int y, int w, Color c);
void px_vline(uint32_t* px, int bw, int bh, int x, int y, int h, Color c);
void px_rect(uint32_t* px, int bw, int bh, int x, int y, int w, int h, Color c);
void px_fill_rounded(uint32_t* px, int bw, int bh, int x, int y, int w, int h, int r, Color c);

// ============================================================================
// Function declarations -- drawing.cpp
// ============================================================================

void canvas_put_pixel(int x, int y, Color c);
void canvas_draw_line(int x0, int y0, int x1, int y1, Color c, int thickness);
void canvas_draw_rect(int x0, int y0, int x1, int y1, Color c, int thickness);
void canvas_fill_rect(int x0, int y0, int x1, int y1, Color c);
void canvas_draw_ellipse(int x0, int y0, int x1, int y1, Color c, int thickness);
void canvas_fill_ellipse(int x0, int y0, int x1, int y1, Color c);
void canvas_flood_fill(int x, int y, Color fill_color);
void canvas_draw_brush(int x, int y, Color c, int size);
void canvas_clear(Color c);

// ============================================================================
// Function declarations -- render.cpp
// ============================================================================

void render(uint32_t* pixels);

// ============================================================================
// Function declarations -- main.cpp
// ============================================================================

void undo_push();
void undo_do();
void redo_do();
int canvas_x();
int canvas_y();
bool screen_to_canvas(int sx, int sy, int* cx, int* cy);
int brush_size();
