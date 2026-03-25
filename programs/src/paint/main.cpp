/*
 * main.cpp
 * Paint app -- global state, event loop, entry point
 * Copyright (c) 2026 Daniel Hammer
 */

#include "paint.h"
#include <gui/standalone.hpp>

// ============================================================================
// Global state definitions
// ============================================================================

int g_win_w = INIT_W;
int g_win_h = INIT_H;

uint32_t* g_canvas = nullptr;
int g_canvas_w = 640;
int g_canvas_h = 480;

uint32_t* g_undo_buf[UNDO_MAX];
int g_undo_count = 0;
int g_undo_pos = 0;

int g_scroll_x = 0;
int g_scroll_y = 0;

Tool g_tool = TOOL_PENCIL;
Color g_fg_color = Color::from_rgb(0x00, 0x00, 0x00);
Color g_bg_color = Color::from_rgb(0xFF, 0xFF, 0xFF);
int g_brush_idx = 0;

bool g_drawing = false;
int g_draw_x0 = 0, g_draw_y0 = 0;
int g_draw_x1 = 0, g_draw_y1 = 0;
int g_prev_x = 0, g_prev_y = 0;

TrueTypeFont* g_font = nullptr;

bool g_modified = false;
char g_filepath[256] = {};

// ============================================================================
// Helpers
// ============================================================================

int brush_size() {
    return BRUSH_SIZES[g_brush_idx];
}

int canvas_x() {
    if (g_canvas_w < g_win_w)
        return (g_win_w - g_canvas_w) / 2 - g_scroll_x;
    return -g_scroll_x;
}

int canvas_y() {
    int area_y = TOOLBAR_H + COLOR_BAR_H;
    int area_h = g_win_h - area_y - STATUS_BAR_H;
    if (g_canvas_h < area_h)
        return area_y + (area_h - g_canvas_h) / 2 - g_scroll_y;
    return area_y - g_scroll_y;
}

bool screen_to_canvas(int sx, int sy, int* cx, int* cy) {
    *cx = sx - canvas_x();
    *cy = sy - canvas_y();
    return *cx >= 0 && *cx < g_canvas_w && *cy >= 0 && *cy < g_canvas_h;
}

// ============================================================================
// Undo/Redo
// ============================================================================

void undo_push() {
    // Discard any redo states after current position
    for (int i = g_undo_pos; i < g_undo_count; i++) {
        if (g_undo_buf[i]) {
            montauk::mfree(g_undo_buf[i]);
            g_undo_buf[i] = nullptr;
        }
    }
    g_undo_count = g_undo_pos;

    // If at capacity, shift everything down
    if (g_undo_count >= UNDO_MAX) {
        if (g_undo_buf[0]) montauk::mfree(g_undo_buf[0]);
        for (int i = 1; i < UNDO_MAX; i++)
            g_undo_buf[i - 1] = g_undo_buf[i];
        g_undo_buf[UNDO_MAX - 1] = nullptr;
        g_undo_count = UNDO_MAX - 1;
        g_undo_pos = g_undo_count;
    }

    int sz = g_canvas_w * g_canvas_h * 4;
    uint32_t* buf = (uint32_t*)montauk::malloc(sz);
    if (buf) {
        montauk::memcpy(buf, g_canvas, sz);
        g_undo_buf[g_undo_count] = buf;
        g_undo_count++;
        g_undo_pos = g_undo_count;
    }
}

void undo_do() {
    if (g_undo_pos <= 0) return;
    // Save current state as redo if we're at the top
    if (g_undo_pos == g_undo_count && g_undo_count < UNDO_MAX) {
        int sz = g_canvas_w * g_canvas_h * 4;
        uint32_t* buf = (uint32_t*)montauk::malloc(sz);
        if (buf) {
            montauk::memcpy(buf, g_canvas, sz);
            g_undo_buf[g_undo_count] = buf;
            g_undo_count++;
        }
    }
    g_undo_pos--;
    int sz = g_canvas_w * g_canvas_h * 4;
    montauk::memcpy(g_canvas, g_undo_buf[g_undo_pos], sz);
}

void redo_do() {
    if (g_undo_pos >= g_undo_count - 1) return;
    g_undo_pos++;
    int sz = g_canvas_w * g_canvas_h * 4;
    montauk::memcpy(g_canvas, g_undo_buf[g_undo_pos], sz);
}

// ============================================================================
// Toolbar hit testing
// ============================================================================

static bool handle_toolbar_click(int mx, int my) {
    if (my >= TOOLBAR_H) return false;

    // Walk the button layout to find which button was clicked
    int bx = 4;
    for (int i = 0; i < TOOL_COUNT; i++) {
        int w = 48;
        int x1 = bx + w;
        if (mx >= bx && mx < x1) {
            g_tool = (Tool)i;
            return true;
        }
        bx = x1 + 4;
        if (i == TOOL_ERASER || i == TOOL_FILLED_ELLIPSE || i == TOOL_EYEDROPPER)
            bx += 8; // separator
    }

    // Brush size button
    {
        int w = 40;
        int x1 = bx + w;
        if (mx >= bx && mx < x1) {
            g_brush_idx = (g_brush_idx + 1) % BRUSH_SIZE_COUNT;
            return true;
        }
    }

    return false;
}

// ============================================================================
// Color bar hit testing
// ============================================================================

static bool handle_colorbar_click(int mx, int my, bool right_click) {
    int cbar_y = TOOLBAR_H;
    if (my < cbar_y || my >= cbar_y + COLOR_BAR_H) return false;

    int preview_sz = COLOR_BAR_H - 8;
    int sw_x = 4 + preview_sz + 16;

    for (int i = 0; i < PALETTE_COUNT; i++) {
        int row = i >= 14 ? 1 : 0;
        int col = i >= 14 ? i - 14 : i;
        int sx = sw_x + col * (SWATCH_SIZE + 2);
        int sy = cbar_y + 2 + row * (SWATCH_SIZE / 2 + 1);
        int sh = SWATCH_SIZE / 2;

        if (mx >= sx && mx < sx + SWATCH_SIZE && my >= sy && my < sy + sh) {
            if (right_click)
                g_bg_color = PALETTE[i];
            else
                g_fg_color = PALETTE[i];
            return true;
        }
    }

    return false;
}

// ============================================================================
// Scroll clamping
// ============================================================================

static void clamp_scroll() {
    int area_h = g_win_h - TOOLBAR_H - COLOR_BAR_H - STATUS_BAR_H;
    int max_sx = g_canvas_w - g_win_w + 40;
    int max_sy = g_canvas_h - area_h + 40;
    if (max_sx < 0) max_sx = 0;
    if (max_sy < 0) max_sy = 0;
    if (g_scroll_x < 0) g_scroll_x = 0;
    if (g_scroll_x > max_sx) g_scroll_x = max_sx;
    if (g_scroll_y < 0) g_scroll_y = 0;
    if (g_scroll_y > max_sy) g_scroll_y = max_sy;
}

// ============================================================================
// Apply shape tool (commit line/rect/ellipse to canvas)
// ============================================================================

static void apply_shape() {
    int bs = brush_size();
    Color c = g_fg_color;

    switch (g_tool) {
    case TOOL_LINE:
        canvas_draw_line(g_draw_x0, g_draw_y0, g_draw_x1, g_draw_y1, c, bs);
        break;
    case TOOL_RECT:
        canvas_draw_rect(g_draw_x0, g_draw_y0, g_draw_x1, g_draw_y1, c, bs);
        break;
    case TOOL_FILLED_RECT:
        canvas_fill_rect(g_draw_x0, g_draw_y0, g_draw_x1, g_draw_y1, c);
        break;
    case TOOL_ELLIPSE:
        canvas_draw_ellipse(g_draw_x0, g_draw_y0, g_draw_x1, g_draw_y1, c, bs);
        break;
    case TOOL_FILLED_ELLIPSE:
        canvas_fill_ellipse(g_draw_x0, g_draw_y0, g_draw_x1, g_draw_y1, c);
        break;
    default:
        break;
    }
}

// ============================================================================
// Entry point
// ============================================================================

extern "C" void _start() {
    // Initialize undo pointers
    for (int i = 0; i < UNDO_MAX; i++) g_undo_buf[i] = nullptr;

    // Allocate canvas
    int canvas_bytes = g_canvas_w * g_canvas_h * 4;
    g_canvas = (uint32_t*)montauk::malloc(canvas_bytes);
    if (!g_canvas) montauk::exit(1);
    canvas_clear(g_bg_color);

    // Load font
    g_font = (TrueTypeFont*)montauk::malloc(sizeof(TrueTypeFont));
    if (g_font) {
        montauk::memset(g_font, 0, sizeof(TrueTypeFont));
        if (!g_font->init("0:/fonts/Roboto-Medium.ttf")) {
            montauk::mfree(g_font);
            g_font = nullptr;
        }
    }

    // Push initial undo state
    undo_push();

    WsWindow win;
    if (!win.create("Paint", INIT_W, INIT_H))
        montauk::exit(1);

    uint32_t* pixels = win.pixels;

    render(pixels);
    win.present();

    while (true) {
        Montauk::WinEvent ev;
        int r = win.poll(&ev);

        if (r < 0) break;

        if (r == 0) {
            montauk::sleep_ms(16);
            continue;
        }

        // Close
        if (ev.type == 3) break;

        // Resize
        if (ev.type == 2) {
            g_win_w = win.width;
            g_win_h = win.height;
            pixels = win.pixels;
            clamp_scroll();
            render(pixels);
            win.present();
            continue;
        }

        bool redraw = false;

        // ============================================================
        // Keyboard
        // ============================================================
        if (ev.type == 0 && ev.key.pressed) {
            auto& key = ev.key;

            // Ctrl+Z: undo
            if (key.ctrl && (key.ascii == 'z' || key.ascii == 'Z')) {
                undo_do();
                redraw = true;
            }
            // Ctrl+Y: redo
            else if (key.ctrl && (key.ascii == 'y' || key.ascii == 'Y')) {
                redo_do();
                redraw = true;
            }
            // Ctrl+N: new canvas (clear)
            else if (key.ctrl && (key.ascii == 'n' || key.ascii == 'N')) {
                undo_push();
                canvas_clear(g_bg_color);
                g_modified = false;
                redraw = true;
            }
            // Tool shortcuts
            else if (!key.ctrl) {
                switch (key.ascii) {
                case 'p': case 'P': g_tool = TOOL_PENCIL; redraw = true; break;
                case 'b': case 'B': g_tool = TOOL_BRUSH; redraw = true; break;
                case 'e': case 'E': g_tool = TOOL_ERASER; redraw = true; break;
                case 'l': case 'L': g_tool = TOOL_LINE; redraw = true; break;
                case 'r': case 'R': g_tool = TOOL_RECT; redraw = true; break;
                case 'o': case 'O': g_tool = TOOL_ELLIPSE; redraw = true; break;
                case 'f': case 'F': g_tool = TOOL_FILL; redraw = true; break;
                case 'i': case 'I': g_tool = TOOL_EYEDROPPER; redraw = true; break;
                case '[':
                    if (g_brush_idx > 0) { g_brush_idx--; redraw = true; }
                    break;
                case ']':
                    if (g_brush_idx < BRUSH_SIZE_COUNT - 1) { g_brush_idx++; redraw = true; }
                    break;
                default: break;
                }
            }
            // Escape: cancel shape drawing
            if (key.scancode == 0x01) {
                if (g_drawing) {
                    g_drawing = false;
                    redraw = true;
                }
            }
        }

        // ============================================================
        // Mouse
        // ============================================================
        if (ev.type == 1) {
            int mx = ev.mouse.x;
            int my = ev.mouse.y;
            uint8_t btns = ev.mouse.buttons;
            uint8_t prev = ev.mouse.prev_buttons;
            bool left_click = (btns & 1) && !(prev & 1);
            bool left_held = (btns & 1);
            bool left_released = !(btns & 1) && (prev & 1);
            bool right_click = (btns & 2) && !(prev & 2);

            // Scroll
            if (ev.mouse.scroll != 0) {
                g_scroll_y -= ev.mouse.scroll * 30;
                clamp_scroll();
                redraw = true;
            }

            // Toolbar click
            if (left_click && my < TOOLBAR_H) {
                if (handle_toolbar_click(mx, my))
                    redraw = true;
                goto done_mouse;
            }

            // Color bar click
            if ((left_click || right_click) && my >= TOOLBAR_H && my < TOOLBAR_H + COLOR_BAR_H) {
                if (handle_colorbar_click(mx, my, right_click))
                    redraw = true;
                goto done_mouse;
            }

            // Canvas interaction
            {
                int cx_pos, cy_pos;
                bool on_canvas = screen_to_canvas(mx, my, &cx_pos, &cy_pos);

                // Eyedropper
                if (g_tool == TOOL_EYEDROPPER && left_click && on_canvas) {
                    uint32_t px = g_canvas[cy_pos * g_canvas_w + cx_pos];
                    g_fg_color = Color::from_rgb((px >> 16) & 0xFF, (px >> 8) & 0xFF, px & 0xFF);
                    redraw = true;
                    goto done_mouse;
                }

                // Fill tool
                if (g_tool == TOOL_FILL && left_click && on_canvas) {
                    undo_push();
                    canvas_flood_fill(cx_pos, cy_pos, g_fg_color);
                    g_modified = true;
                    redraw = true;
                    goto done_mouse;
                }

                // Pencil / Brush / Eraser: freehand drawing
                if ((g_tool == TOOL_PENCIL || g_tool == TOOL_BRUSH || g_tool == TOOL_ERASER)) {
                    Color draw_c = (g_tool == TOOL_ERASER) ? g_bg_color : g_fg_color;
                    int bs = brush_size();
                    if (g_tool == TOOL_PENCIL) bs = 1;
                    if (g_tool == TOOL_BRUSH) {
                        if (bs < 4) bs = 4;
                    }

                    if (left_click && on_canvas) {
                        undo_push();
                        g_drawing = true;
                        g_prev_x = cx_pos;
                        g_prev_y = cy_pos;
                        canvas_draw_brush(cx_pos, cy_pos, draw_c, bs);
                        g_modified = true;
                        redraw = true;
                    } else if (g_drawing && left_held) {
                        if (on_canvas && (cx_pos != g_prev_x || cy_pos != g_prev_y)) {
                            canvas_draw_line(g_prev_x, g_prev_y, cx_pos, cy_pos, draw_c, bs);
                            g_prev_x = cx_pos;
                            g_prev_y = cy_pos;
                            redraw = true;
                        }
                    }
                    if (left_released && g_drawing) {
                        g_drawing = false;
                        redraw = true;
                    }
                    goto done_mouse;
                }

                // Shape tools: line, rect, ellipse
                if (g_tool == TOOL_LINE || g_tool == TOOL_RECT || g_tool == TOOL_FILLED_RECT ||
                    g_tool == TOOL_ELLIPSE || g_tool == TOOL_FILLED_ELLIPSE) {

                    if (left_click && on_canvas) {
                        undo_push();
                        g_drawing = true;
                        g_draw_x0 = cx_pos;
                        g_draw_y0 = cy_pos;
                        g_draw_x1 = cx_pos;
                        g_draw_y1 = cy_pos;
                        redraw = true;
                    } else if (g_drawing && left_held) {
                        if (on_canvas) {
                            g_draw_x1 = cx_pos;
                            g_draw_y1 = cy_pos;
                            redraw = true;
                        }
                    }
                    if (left_released && g_drawing) {
                        if (on_canvas) {
                            g_draw_x1 = cx_pos;
                            g_draw_y1 = cy_pos;
                        }
                        apply_shape();
                        g_drawing = false;
                        g_modified = true;
                        redraw = true;
                    }
                    goto done_mouse;
                }
            }

            done_mouse:;
        }

        if (redraw) {
            render(pixels);
            win.present();
        }
    }

    win.destroy();
    montauk::exit(0);
}
