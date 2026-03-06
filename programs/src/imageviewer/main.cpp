/*
 * main.cpp
 * MontaukOS Image Viewer - standalone Window Server process
 * Displays images with zoom, pan, fit-to-window, and 1:1 modes
 * Copyright (c) 2026 Daniel Hammer
 */

#include <montauk/syscall.h>
#include <montauk/string.h>
#include <montauk/heap.h>
#include <gui/gui.hpp>
#include <gui/truetype.hpp>

extern "C" {
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <gui/stb_image.h>
}

using namespace gui;

// ============================================================================
// Constants
// ============================================================================

static constexpr int INIT_W        = 800;
static constexpr int INIT_H        = 600;
static constexpr int TOOLBAR_H     = 36;
static constexpr int STATUS_BAR_H  = 24;
static constexpr int PAN_STEP      = 40;
static constexpr int TB_BTN_SIZE   = 24;
static constexpr int TB_BTN_Y      = 6;
static constexpr int TB_BTN_RAD    = 3;
static constexpr int HEADER_FONT   = 16;

static constexpr Color BG_COLOR      = Color::from_rgb(0x30, 0x30, 0x30);
static constexpr Color TOOLBAR_BG    = Color::from_rgb(0xF5, 0xF5, 0xF5);
static constexpr Color TB_BTN_BG     = Color::from_rgb(0xE8, 0xE8, 0xE8);
static constexpr Color TB_BTN_ACTIVE = Color::from_rgb(0xC0, 0xD0, 0xE8);
static constexpr Color TB_SEP_COLOR  = Color::from_rgb(0xCC, 0xCC, 0xCC);
static constexpr Color GRID_COLOR    = Color::from_rgb(0xD0, 0xD0, 0xD0);
static constexpr Color HEADER_TEXT   = Color::from_rgb(0x55, 0x55, 0x55);
static constexpr Color STATUS_BG     = Color::from_rgb(0x2B, 0x3E, 0x50);
static constexpr Color STATUS_TEXT   = Color::from_rgb(0xFF, 0xFF, 0xFF);
static constexpr Color ERR_COLOR     = Color::from_rgb(0xCC, 0x33, 0x33);
static constexpr Color CHECK_LIGHT   = Color::from_rgb(0xCC, 0xCC, 0xCC);
static constexpr Color CHECK_DARK    = Color::from_rgb(0x99, 0x99, 0x99);

// Zoom levels (percentage values / 100)
static constexpr float ZOOM_MIN = 0.05f;
static constexpr float ZOOM_MAX = 16.0f;

// ============================================================================
// App state
// ============================================================================

static int       g_win_w   = INIT_W;
static int       g_win_h   = INIT_H;

// Image data (original, unscaled)
static uint32_t* g_image   = nullptr;
static int       g_img_w   = 0;
static int       g_img_h   = 0;
static bool      g_has_alpha = false;

// Zoom and pan
static float     g_zoom    = 1.0f;
static int       g_pan_x   = 0;
static int       g_pan_y   = 0;

// Mouse drag state
static bool      g_dragging       = false;
static int       g_drag_start_x   = 0;
static int       g_drag_start_y   = 0;
static int       g_drag_pan_x     = 0;
static int       g_drag_pan_y     = 0;

// File info
static char      g_filepath[512]  = {};
static char      g_status[256]    = {};
static bool      g_load_ok        = false;

// Font
static TrueTypeFont* g_font      = nullptr;

// Toolbar button X positions (computed during render)
static int tb_zoom_out_x0, tb_zoom_out_x1;
static int tb_zoom_in_x0, tb_zoom_in_x1;
static int tb_fit_x0, tb_fit_x1;
static int tb_actual_x0, tb_actual_x1;
static int tb_open_x0, tb_open_x1;

// ============================================================================
// Helpers
// ============================================================================

static void px_fill(uint32_t* px, int bw, int bh,
                    int x, int y, int w, int h, Color c) {
    uint32_t v = c.to_pixel();
    int x0 = x < 0 ? 0 : x,   y0 = y < 0 ? 0 : y;
    int x1 = x + w > bw ? bw : x + w;
    int y1 = y + h > bh ? bh : y + h;
    for (int row = y0; row < y1; row++)
        for (int col = x0; col < x1; col++)
            px[row * bw + col] = v;
}

static void px_hline(uint32_t* px, int bw, int bh, int x, int y, int w, Color c) {
    if (y < 0 || y >= bh) return;
    uint32_t v = c.to_pixel();
    int x0 = x < 0 ? 0 : x;
    int x1 = x + w > bw ? bw : x + w;
    for (int col = x0; col < x1; col++)
        px[y * bw + col] = v;
}

static void px_vline(uint32_t* px, int bw, int bh, int x, int y, int h, Color c) {
    if (x < 0 || x >= bw) return;
    uint32_t v = c.to_pixel();
    int y0 = y < 0 ? 0 : y;
    int y1 = y + h > bh ? bh : y + h;
    for (int row = y0; row < y1; row++)
        px[row * bw + x] = v;
}

static void px_fill_rounded(uint32_t* px, int bw, int bh,
                             int x, int y, int w, int h, int r, Color c) {
    uint32_t v = c.to_pixel();
    for (int row = 0; row < h; row++) {
        int dy = y + row;
        if (dy < 0 || dy >= bh) continue;
        for (int col = 0; col < w; col++) {
            int dx = x + col;
            if (dx < 0 || dx >= bw) continue;
            // Check corners
            bool skip = false;
            int cx, cy;
            if (col < r && row < r) { cx = r - col - 1; cy = r - row - 1; if (cx*cx + cy*cy >= r*r) skip = true; }
            else if (col >= w - r && row < r) { cx = col - (w - r); cy = r - row - 1; if (cx*cx + cy*cy >= r*r) skip = true; }
            else if (col < r && row >= h - r) { cx = r - col - 1; cy = row - (h - r); if (cx*cx + cy*cy >= r*r) skip = true; }
            else if (col >= w - r && row >= h - r) { cx = col - (w - r); cy = row - (h - r); if (cx*cx + cy*cy >= r*r) skip = true; }
            if (!skip) px[dy * bw + dx] = v;
        }
    }
}

static const char* basename(const char* path) {
    const char* last = path;
    for (const char* p = path; *p; p++) {
        if (*p == '/') last = p + 1;
    }
    return last;
}

// ============================================================================
// Image loading
// ============================================================================

static bool load_image(const char* path) {
    if (g_image) { montauk::mfree(g_image); g_image = nullptr; }

    int fd = montauk::open(path);
    if (fd < 0) {
        montauk::strcpy(g_status, "Error: could not open file");
        return false;
    }

    uint64_t size = montauk::getsize(fd);
    if (size == 0 || size > 32 * 1024 * 1024) {
        montauk::close(fd);
        montauk::strcpy(g_status, "Error: file too large or empty");
        return false;
    }

    uint8_t* filedata = (uint8_t*)montauk::malloc(size);
    if (!filedata) {
        montauk::close(fd);
        montauk::strcpy(g_status, "Error: out of memory");
        return false;
    }

    int bytes_read = montauk::read(fd, filedata, 0, size);
    montauk::close(fd);

    if (bytes_read <= 0) {
        montauk::mfree(filedata);
        montauk::strcpy(g_status, "Error: could not read file");
        return false;
    }

    int w, h, channels;
    unsigned char* rgba = stbi_load_from_memory(filedata, bytes_read, &w, &h, &channels, 4);
    montauk::mfree(filedata);

    if (!rgba) {
        snprintf(g_status, 256, "Error: %s", stbi_failure_reason() ? stbi_failure_reason() : "decode failed");
        return false;
    }

    g_has_alpha = (channels == 4 || channels == 2);

    // Convert RGBA to ARGB pixel format
    g_image = (uint32_t*)montauk::malloc((uint64_t)w * h * 4);
    if (!g_image) {
        stbi_image_free(rgba);
        montauk::strcpy(g_status, "Error: out of memory for image");
        return false;
    }

    for (int i = 0; i < w * h; i++) {
        uint8_t r = rgba[i * 4 + 0];
        uint8_t g = rgba[i * 4 + 1];
        uint8_t b = rgba[i * 4 + 2];
        uint8_t a = rgba[i * 4 + 3];
        g_image[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }

    stbi_image_free(rgba);

    g_img_w = w;
    g_img_h = h;

    snprintf(g_status, 256, "%s  %dx%d  %dch", basename(path), w, h, channels);
    return true;
}

// ============================================================================
// Zoom and pan
// ============================================================================

static int viewport_h() { return g_win_h - TOOLBAR_H - STATUS_BAR_H; }

static void clamp_pan() {
    int scaled_w = (int)(g_img_w * g_zoom);
    int scaled_h = (int)(g_img_h * g_zoom);
    int vp_h = viewport_h();

    if (scaled_w <= g_win_w) {
        g_pan_x = (g_win_w - scaled_w) / 2;
    } else {
        if (g_pan_x > 0) g_pan_x = 0;
        if (g_pan_x < g_win_w - scaled_w) g_pan_x = g_win_w - scaled_w;
    }

    if (scaled_h <= vp_h) {
        g_pan_y = (vp_h - scaled_h) / 2 + TOOLBAR_H;
    } else {
        if (g_pan_y > TOOLBAR_H) g_pan_y = TOOLBAR_H;
        if (g_pan_y < TOOLBAR_H + vp_h - scaled_h) g_pan_y = TOOLBAR_H + vp_h - scaled_h;
    }
}

static void center_image() {
    int scaled_w = (int)(g_img_w * g_zoom);
    int scaled_h = (int)(g_img_h * g_zoom);
    int vp_h = viewport_h();
    g_pan_x = (g_win_w - scaled_w) / 2;
    g_pan_y = (vp_h - scaled_h) / 2 + TOOLBAR_H;
    clamp_pan();
}

static void zoom_to(float new_zoom, int focus_x, int focus_y) {
    if (new_zoom < ZOOM_MIN) new_zoom = ZOOM_MIN;
    if (new_zoom > ZOOM_MAX) new_zoom = ZOOM_MAX;
    if (new_zoom == g_zoom) return;

    // Zoom toward the focus point (mouse position or center)
    float ratio = new_zoom / g_zoom;
    g_pan_x = focus_x - (int)((focus_x - g_pan_x) * ratio);
    g_pan_y = focus_y - (int)((focus_y - g_pan_y) * ratio);
    g_zoom = new_zoom;
    clamp_pan();
}

static void zoom_in(int fx, int fy) {
    // Step to next "nice" level
    float next;
    if (g_zoom < 0.1f) next = g_zoom + 0.05f;
    else if (g_zoom < 0.5f) next = g_zoom + 0.1f;
    else if (g_zoom < 2.0f) next = g_zoom + 0.25f;
    else if (g_zoom < 4.0f) next = g_zoom + 0.5f;
    else next = g_zoom + 1.0f;
    zoom_to(next, fx, fy);
}

static void zoom_out(int fx, int fy) {
    float next;
    if (g_zoom <= 0.1f) next = g_zoom - 0.05f;
    else if (g_zoom <= 0.5f) next = g_zoom - 0.1f;
    else if (g_zoom <= 2.0f) next = g_zoom - 0.25f;
    else if (g_zoom <= 4.0f) next = g_zoom - 0.5f;
    else next = g_zoom - 1.0f;
    zoom_to(next, fx, fy);
}

static void zoom_fit() {
    if (!g_load_ok) return;
    int vp_h = viewport_h();
    float zx = (float)g_win_w / g_img_w;
    float zy = (float)vp_h / g_img_h;
    g_zoom = zx < zy ? zx : zy;
    if (g_zoom > ZOOM_MAX) g_zoom = ZOOM_MAX;
    center_image();
}

static void zoom_actual() {
    int cx = g_win_w / 2;
    int cy = TOOLBAR_H + viewport_h() / 2;
    zoom_to(1.0f, cx, cy);
}

// ============================================================================
// Rendering
// ============================================================================

static void render(uint32_t* pixels) {
    int vp_y0 = TOOLBAR_H;
    int vp_y1 = g_win_h - STATUS_BAR_H;

    // Fill viewport background
    px_fill(pixels, g_win_w, g_win_h, 0, vp_y0, g_win_w, vp_y1 - vp_y0, BG_COLOR);

    // Draw scaled image
    if (g_image && g_load_ok) {
        int scaled_w = (int)(g_img_w * g_zoom);
        int scaled_h = (int)(g_img_h * g_zoom);

        // Viewport-clipped drawing region
        int draw_x0 = g_pan_x < 0 ? 0 : g_pan_x;
        int draw_y0 = g_pan_y < vp_y0 ? vp_y0 : g_pan_y;
        int draw_x1 = g_pan_x + scaled_w > g_win_w ? g_win_w : g_pan_x + scaled_w;
        int draw_y1 = g_pan_y + scaled_h > vp_y1 ? vp_y1 : g_pan_y + scaled_h;

        float inv_zoom = 1.0f / g_zoom;

        for (int dy = draw_y0; dy < draw_y1; dy++) {
            int src_y = (int)((dy - g_pan_y) * inv_zoom);
            if (src_y < 0) src_y = 0;
            if (src_y >= g_img_h) src_y = g_img_h - 1;

            const uint32_t* src_row = &g_image[src_y * g_img_w];
            uint32_t* dst_row = &pixels[dy * g_win_w];

            for (int dx = draw_x0; dx < draw_x1; dx++) {
                int src_x = (int)((dx - g_pan_x) * inv_zoom);
                if (src_x < 0) src_x = 0;
                if (src_x >= g_img_w) src_x = g_img_w - 1;

                uint32_t spx = src_row[src_x];
                uint8_t a = (spx >> 24) & 0xFF;

                if (a == 255) {
                    dst_row[dx] = spx | 0xFF000000u;
                } else if (a > 0) {
                    // Alpha blend over checkerboard
                    int cx = (dx >> 3) & 1;
                    int cy = (dy >> 3) & 1;
                    uint32_t check = (cx ^ cy) ? CHECK_LIGHT.to_pixel() : CHECK_DARK.to_pixel();
                    uint8_t br = (check >> 16) & 0xFF;
                    uint8_t bg = (check >> 8) & 0xFF;
                    uint8_t bb = check & 0xFF;
                    uint8_t sr = (spx >> 16) & 0xFF;
                    uint8_t sg = (spx >> 8) & 0xFF;
                    uint8_t sb = spx & 0xFF;
                    uint32_t inv = 255 - a;
                    uint8_t rr = (a * sr + inv * br + 128) / 255;
                    uint8_t rg = (a * sg + inv * bg + 128) / 255;
                    uint8_t rb = (a * sb + inv * bb + 128) / 255;
                    dst_row[dx] = 0xFF000000u | ((uint32_t)rr << 16) | ((uint32_t)rg << 8) | rb;
                } else {
                    // Fully transparent — show checkerboard
                    int cx = (dx >> 3) & 1;
                    int cy = (dy >> 3) & 1;
                    dst_row[dx] = (cx ^ cy) ? CHECK_LIGHT.to_pixel() : CHECK_DARK.to_pixel();
                }
            }
        }
    } else if (!g_load_ok && g_font) {
        g_font->draw_to_buffer(pixels, g_win_w, g_win_h,
            20, vp_y0 + (vp_y1 - vp_y0) / 2 - 8, g_status, ERR_COLOR, 15);
    }

    // ---- Toolbar ----
    px_fill(pixels, g_win_w, g_win_h, 0, 0, g_win_w, TOOLBAR_H, TOOLBAR_BG);
    px_hline(pixels, g_win_w, g_win_h, 0, TOOLBAR_H - 1, g_win_w, GRID_COLOR);

    int bx = 4;
    auto tb_btn = [&](int w, bool active, const char* label, int& x0_out, int& x1_out) {
        x0_out = bx;
        x1_out = bx + w;
        Color bg = active ? TB_BTN_ACTIVE : TB_BTN_BG;
        px_fill_rounded(pixels, g_win_w, g_win_h, bx, TB_BTN_Y, w, TB_BTN_SIZE, TB_BTN_RAD, bg);
        if (g_font && label[0]) {
            int tw = g_font->measure_text(label, HEADER_FONT);
            g_font->draw_to_buffer(pixels, g_win_w, g_win_h,
                bx + (w - tw) / 2, TB_BTN_Y + (TB_BTN_SIZE - HEADER_FONT) / 2,
                label, HEADER_TEXT, HEADER_FONT);
        }
        bx += w + 4;
    };
    auto tb_sep = [&]() {
        px_vline(pixels, g_win_w, g_win_h, bx, 6, TOOLBAR_H - 12, TB_SEP_COLOR);
        bx += 8;
    };

    // Open
    tb_btn(36, false, "Open", tb_open_x0, tb_open_x1);
    tb_sep();

    // Zoom controls
    tb_btn(24, false, "-", tb_zoom_out_x0, tb_zoom_out_x1);
    tb_btn(24, false, "+", tb_zoom_in_x0, tb_zoom_in_x1);

    // Zoom percentage label
    {
        char zoom_label[16];
        int pct = (int)(g_zoom * 100 + 0.5f);
        snprintf(zoom_label, 16, "%d%%", pct);
        if (g_font) {
            int tw = g_font->measure_text(zoom_label, HEADER_FONT);
            g_font->draw_to_buffer(pixels, g_win_w, g_win_h,
                bx, TB_BTN_Y + (TB_BTN_SIZE - HEADER_FONT) / 2,
                zoom_label, HEADER_TEXT, HEADER_FONT);
            bx += tw + 8;
        }
    }

    tb_sep();

    // Fit / 1:1
    tb_btn(28, false, "Fit", tb_fit_x0, tb_fit_x1);
    tb_btn(28, false, "1:1", tb_actual_x0, tb_actual_x1);

    // ---- Status bar ----
    int sy = g_win_h - STATUS_BAR_H;
    px_fill(pixels, g_win_w, g_win_h, 0, sy, g_win_w, STATUS_BAR_H, STATUS_BG);

    if (g_font) {
        int sty = sy + (STATUS_BAR_H - HEADER_FONT) / 2;

        // Left: filename and dimensions
        if (g_load_ok && g_status[0]) {
            g_font->draw_to_buffer(pixels, g_win_w, g_win_h, 8, sty,
                g_status, STATUS_TEXT, HEADER_FONT);
        } else if (!g_load_ok) {
            g_font->draw_to_buffer(pixels, g_win_w, g_win_h, 8, sty,
                "No image loaded", STATUS_TEXT, HEADER_FONT);
        }

        // Right: zoom level
        if (g_load_ok) {
            char right[32];
            int pct = (int)(g_zoom * 100 + 0.5f);
            snprintf(right, 32, "%d%% ", pct);
            int rw = g_font->measure_text(right, HEADER_FONT);
            g_font->draw_to_buffer(pixels, g_win_w, g_win_h,
                g_win_w - rw - 8, sty, right, STATUS_TEXT, HEADER_FONT);
        }
    }
}

// ============================================================================
// Toolbar hit test
// ============================================================================

static bool handle_toolbar_click(int mx, int my) {
    if (my >= TOOLBAR_H || my < TB_BTN_Y || my >= TB_BTN_Y + TB_BTN_SIZE) return false;

    int cx = g_win_w / 2;
    int cy = TOOLBAR_H + viewport_h() / 2;

    if (mx >= tb_open_x0 && mx < tb_open_x1) {
        // TODO: open file dialog
        return true;
    }
    if (mx >= tb_zoom_out_x0 && mx < tb_zoom_out_x1) {
        zoom_out(cx, cy);
        return true;
    }
    if (mx >= tb_zoom_in_x0 && mx < tb_zoom_in_x1) {
        zoom_in(cx, cy);
        return true;
    }
    if (mx >= tb_fit_x0 && mx < tb_fit_x1) {
        zoom_fit();
        return true;
    }
    if (mx >= tb_actual_x0 && mx < tb_actual_x1) {
        zoom_actual();
        return true;
    }
    return false;
}

// ============================================================================
// Entry point
// ============================================================================

extern "C" void _start() {
    // Get file path from arguments
    int arglen = montauk::getargs(g_filepath, sizeof(g_filepath));
    if (arglen <= 0) g_filepath[0] = '\0';

    // Load font
    {
        TrueTypeFont* f = (TrueTypeFont*)montauk::malloc(sizeof(TrueTypeFont));
        if (f) {
            montauk::memset(f, 0, sizeof(TrueTypeFont));
            if (!f->init("0:/fonts/Roboto-Medium.ttf")) { montauk::mfree(f); f = nullptr; }
        }
        g_font = f;
    }

    // Load image
    if (g_filepath[0]) {
        g_load_ok = load_image(g_filepath);
    } else {
        montauk::strcpy(g_status, "No file specified");
        g_load_ok = false;
    }

    // Build window title
    char title[128] = "Image Viewer";
    if (g_filepath[0]) {
        const char* name = basename(g_filepath);
        if (name[0]) {
            snprintf(title, sizeof(title), "%s (%dx%d)", name, g_img_w, g_img_h);
        }
    }

    // Create window
    Montauk::WinCreateResult wres;
    if (montauk::win_create(title, INIT_W, INIT_H, &wres) < 0 || wres.id < 0)
        montauk::exit(1);

    int       win_id = wres.id;
    uint32_t* pixels = (uint32_t*)(uintptr_t)wres.pixelVa;

    // Auto-fit on load if image is larger than viewport
    if (g_load_ok) {
        if (g_img_w > g_win_w || g_img_h > viewport_h()) {
            zoom_fit();
        } else {
            center_image();
        }
    }

    render(pixels);
    montauk::win_present(win_id);

    // Event loop
    while (true) {
        Montauk::WinEvent ev;
        int r = montauk::win_poll(win_id, &ev);

        if (r < 0) break;
        if (r == 0) { montauk::sleep_ms(16); continue; }

        if (ev.type == 3) break; // close

        // Resize
        if (ev.type == 2) {
            g_win_w = ev.resize.w;
            g_win_h = ev.resize.h;
            pixels = (uint32_t*)(uintptr_t)montauk::win_resize(win_id, g_win_w, g_win_h);
            if (g_load_ok) clamp_pan();
            render(pixels);
            montauk::win_present(win_id);
            continue;
        }

        bool redraw = false;

        // Keyboard
        if (ev.type == 0 && ev.key.pressed) {
            auto& key = ev.key;
            int cx = g_win_w / 2;
            int cy = TOOLBAR_H + viewport_h() / 2;

            if (key.ascii == 'q' || key.ascii == 'Q' || key.scancode == 0x01) break;

            // Zoom keys
            if (key.ascii == '+' || key.ascii == '=') { zoom_in(cx, cy); redraw = true; }
            else if (key.ascii == '-') { zoom_out(cx, cy); redraw = true; }
            else if (key.ascii == '0') { zoom_fit(); redraw = true; }
            else if (key.ascii == '1') { zoom_actual(); redraw = true; }
            // Ctrl+= / Ctrl+-
            else if (key.ctrl && (key.ascii == '+' || key.ascii == '=' || key.ascii == 29)) { zoom_in(cx, cy); redraw = true; }
            else if (key.ctrl && (key.ascii == '-' || key.ascii == 31)) { zoom_out(cx, cy); redraw = true; }

            // Arrow keys to pan
            else if (key.scancode == 0x48) { g_pan_y += PAN_STEP; redraw = true; }
            else if (key.scancode == 0x50) { g_pan_y -= PAN_STEP; redraw = true; }
            else if (key.scancode == 0x4B) { g_pan_x += PAN_STEP; redraw = true; }
            else if (key.scancode == 0x4D) { g_pan_x -= PAN_STEP; redraw = true; }

            // Home to center
            else if (key.scancode == 0x47) { center_image(); redraw = true; }

            if (redraw && g_load_ok) clamp_pan();
        }

        // Mouse
        if (ev.type == 1) {
            int mx = ev.mouse.x;
            int my = ev.mouse.y;
            bool left_now  = ev.mouse.buttons & 1;
            bool left_prev = ev.mouse.prev_buttons & 1;
            bool clicked   = left_now && !left_prev;

            // Toolbar click
            if (clicked && my < TOOLBAR_H) {
                if (handle_toolbar_click(mx, my))
                    redraw = true;
            }

            // Start drag (in viewport area)
            else if (clicked && my >= TOOLBAR_H && my < g_win_h - STATUS_BAR_H) {
                g_dragging = true;
                g_drag_start_x = mx;
                g_drag_start_y = my;
                g_drag_pan_x = g_pan_x;
                g_drag_pan_y = g_pan_y;
            }

            // Continue drag
            if (left_now && g_dragging) {
                g_pan_x = g_drag_pan_x + (mx - g_drag_start_x);
                g_pan_y = g_drag_pan_y + (my - g_drag_start_y);
                redraw = true;
            }

            // End drag
            if (!left_now && g_dragging) {
                g_dragging = false;
            }

            // Scroll wheel zooms toward mouse position
            if (ev.mouse.scroll != 0 && my >= TOOLBAR_H && my < g_win_h - STATUS_BAR_H) {
                if (ev.mouse.scroll > 0) zoom_in(mx, my);
                else zoom_out(mx, my);
                redraw = true;
            }

            if (redraw && g_load_ok) clamp_pan();
        }

        if (redraw) {
            render(pixels);
            montauk::win_present(win_id);
        }
    }

    if (g_image) montauk::mfree(g_image);
    montauk::win_destroy(win_id);
    montauk::exit(0);
}
