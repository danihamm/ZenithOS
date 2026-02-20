/*
    * app_wiki.cpp
    * ZenithOS Desktop - Wikipedia client application
    * Spawns wiki.elf -d as a child process for non-blocking network I/O
    * Copyright (c) 2026 Daniel Hammer
*/

#include "apps_common.hpp"

// ============================================================================
// Constants
// ============================================================================

static constexpr int WIKI_RESP_MAX     = 131072;  // 128 KB
static constexpr int WIKI_MAX_LINES    = 4096;
static constexpr int WIKI_TOOLBAR_H    = 36;
static constexpr int WIKI_SCROLLBAR_W  = 12;

// ============================================================================
// Wiki state
// ============================================================================

struct WikiDisplayLine {
    char text[256];
    Color color;
};

struct WikiState {
    DesktopState* ds;

    enum Mode { IDLE, FETCHING, DONE, WIKI_ERROR };
    Mode mode;
    char searchQuery[256];

    WikiDisplayLine* lines;
    int lineCount;
    int lineCap;
    int scrollY;    // scroll offset in lines

    // Child process for non-blocking fetch
    int child_pid;
    char* respBuf;      // accumulated child output
    int respPos;        // current position in respBuf

    char statusMsg[128];
};

// ============================================================================
// JSON string extraction (for parsing child output)
// ============================================================================

static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }

static bool memeq(const char* a, const char* b, int n) {
    for (int i = 0; i < n; i++) if (a[i] != b[i]) return false;
    return true;
}

static int wiki_extract_json_string(const char* buf, int len, const char* key,
                                    char* out, int maxOut) {
    int klen = slen(key);

    for (int i = 0; i < len - klen - 3; i++) {
        if (buf[i] != '"') continue;
        if (!memeq(buf + i + 1, key, klen)) continue;
        if (buf[i + 1 + klen] != '"') continue;
        if (buf[i + 2 + klen] != ':') continue;

        int p = i + 3 + klen;
        while (p < len && (buf[p] == ' ' || buf[p] == '\t')) p++;
        if (p >= len || buf[p] != '"') continue;
        p++;

        int j = 0;
        while (p < len && j < maxOut - 4) {
            if (buf[p] == '"') break;
            if (buf[p] == '\\' && p + 1 < len) {
                p++;
                switch (buf[p]) {
                case '"':  out[j++] = '"'; break;
                case '\\': out[j++] = '\\'; break;
                case 'n':  out[j++] = '\n'; break;
                case 'r':  break;
                case 't':  out[j++] = '\t'; break;
                case '/':  out[j++] = '/'; break;
                case 'u': {
                    if (p + 4 < len) {
                        unsigned val = 0;
                        for (int k = 1; k <= 4; k++) {
                            char h = buf[p + k];
                            val <<= 4;
                            if (h >= '0' && h <= '9') val |= h - '0';
                            else if (h >= 'a' && h <= 'f') val |= h - 'a' + 10;
                            else if (h >= 'A' && h <= 'F') val |= h - 'A' + 10;
                        }
                        p += 4;
                        if (val < 128) out[j++] = (char)val;
                        else if (val == 0x2013 || val == 0x2014) out[j++] = '-';
                        else if (val == 0x2018 || val == 0x2019) out[j++] = '\'';
                        else if (val == 0x201C || val == 0x201D) out[j++] = '"';
                        else if (val == 0x2026) { out[j++] = '.'; out[j++] = '.'; out[j++] = '.'; }
                        else out[j++] = '?';
                    }
                    break;
                }
                default: out[j++] = buf[p]; break;
                }
            } else {
                out[j++] = buf[p];
            }
            p++;
        }
        out[j] = '\0';
        return j;
    }
    out[0] = '\0';
    return 0;
}

// ============================================================================
// Display line building (word-wrap adapted for pixel widths)
// ============================================================================

static void wiki_add_line(WikiState* ws, const char* text, int len, Color color) {
    if (ws->lineCount >= ws->lineCap) return;
    WikiDisplayLine* dl = &ws->lines[ws->lineCount];
    int copyLen = len;
    if (copyLen > 255) copyLen = 255;
    for (int i = 0; i < copyLen; i++) dl->text[i] = text[i];
    dl->text[copyLen] = '\0';
    dl->color = color;
    ws->lineCount++;
}

static void wiki_wrap_text(WikiState* ws, const char* text, int textLen,
                           int maxChars, Color color) {
    if (textLen <= 0 || maxChars <= 0) return;
    const char* p = text;
    const char* end = text + textLen;

    while (p < end && ws->lineCount < ws->lineCap) {
        while (p < end && *p == ' ') p++;
        if (p >= end) break;

        const char* lineStart = p;
        const char* lastSpace = nullptr;
        int col = 0;

        while (p < end && col < maxChars) {
            if (*p == ' ') lastSpace = p;
            p++;
            col++;
        }

        if (p >= end) {
            wiki_add_line(ws, lineStart, (int)(p - lineStart), color);
        } else if (lastSpace && lastSpace > lineStart) {
            wiki_add_line(ws, lineStart, (int)(lastSpace - lineStart), color);
            p = lastSpace + 1;
        } else {
            wiki_add_line(ws, lineStart, (int)(p - lineStart), color);
        }
    }
}

static void wiki_build_display(WikiState* ws, const char* title,
                                const char* extract, int extractLen, int contentW) {
    ws->lineCount = 0;
    ws->scrollY = 0;

    int char_w = mono_cell_width();
    int maxChars = (contentW - 24 - WIKI_SCROLLBAR_W) / char_w;
    if (maxChars < 20) maxChars = 20;

    Color accent_c = colors::ACCENT;
    Color green_c = Color::from_rgb(0x2E, 0x7D, 0x32);
    Color text_c = colors::TEXT_COLOR;

    // Title
    if (title && title[0]) {
        wiki_wrap_text(ws, title, slen(title), maxChars, accent_c);
    }

    // Blank separator
    if (ws->lineCount > 0)
        wiki_add_line(ws, "", 0, text_c);

    // Process extract line by line
    const char* p = extract;
    const char* end = extract + extractLen;

    while (p < end && ws->lineCount < ws->lineCap) {
        const char* lineStart = p;
        while (p < end && *p != '\n') p++;
        int lineLen = (int)(p - lineStart);
        if (p < end) p++;

        if (lineLen == 0) {
            wiki_add_line(ws, "", 0, text_c);
            continue;
        }

        // Section header detection (== Title ==)
        if (lineLen >= 4 && lineStart[0] == '=' && lineStart[1] == '=') {
            int si = 0;
            while (si < lineLen && lineStart[si] == '=') si++;
            while (si < lineLen && lineStart[si] == ' ') si++;
            int ei = lineLen;
            while (ei > si && lineStart[ei - 1] == '=') ei--;
            while (ei > si && lineStart[ei - 1] == ' ') ei--;

            wiki_add_line(ws, "", 0, text_c);
            if (ei > si) {
                char secBuf[256];
                int secLen = ei - si;
                if (secLen > 255) secLen = 255;
                for (int i = 0; i < secLen; i++) secBuf[i] = lineStart[si + i];
                secBuf[secLen] = '\0';
                wiki_add_line(ws, secBuf, secLen, green_c);
            }
            continue;
        }

        // Regular text
        wiki_wrap_text(ws, lineStart, lineLen, maxChars, text_c);
    }
}

// ============================================================================
// Process completed response from child
// ============================================================================

static void wiki_process_response(WikiState* ws) {
    const char* body = ws->respBuf;
    int bodyLen = ws->respPos;

    if (bodyLen <= 0) {
        snprintf(ws->statusMsg, sizeof(ws->statusMsg), "Error: no response from Wikipedia");
        ws->mode = WikiState::WIKI_ERROR;
        return;
    }

    // Check for error sentinel from child
    if (bodyLen >= 1 && body[0] == '\x01') {
        snprintf(ws->statusMsg, sizeof(ws->statusMsg), "Article not found: %s", ws->searchQuery);
        ws->mode = WikiState::WIKI_ERROR;
        return;
    }

    static char title[512], extractBuf[131072];
    wiki_extract_json_string(body, bodyLen, "title", title, sizeof(title));
    int extractLen = wiki_extract_json_string(body, bodyLen, "extract",
                                              extractBuf, sizeof(extractBuf) - 1);

    if (extractLen == 0) {
        snprintf(ws->statusMsg, sizeof(ws->statusMsg), "No content found for: %s", ws->searchQuery);
        ws->mode = WikiState::WIKI_ERROR;
        return;
    }

    // Find content width from the window
    int contentW = 580;
    for (int i = 0; i < ws->ds->window_count; i++) {
        Window* w = &ws->ds->windows[i];
        if (w->app_data == ws) {
            Rect cr = w->content_rect();
            contentW = cr.w;
            break;
        }
    }

    wiki_build_display(ws, title, extractBuf, extractLen, contentW);
    ws->mode = WikiState::DONE;
}

// ============================================================================
// Callbacks
// ============================================================================

static void wiki_on_draw(Window* win, Framebuffer& fb) {
    WikiState* ws = (WikiState*)win->app_data;
    if (!ws) return;

    Canvas c(win);
    c.fill(colors::WINDOW_BG);

    // ---- Toolbar background ----
    c.fill_rect(0, 0, c.w, WIKI_TOOLBAR_H, Color::from_rgb(0xF5, 0xF5, 0xF5));

    // Toolbar separator line
    c.hline(0, WIKI_TOOLBAR_H, c.w, colors::BORDER);

    // ---- Draw search box outline ----
    int tb_x = 8;
    int tb_y = 6;
    int tb_w = c.w - 90;
    int tb_h = 24;
    if (tb_w < 100) tb_w = 100;

    // TextBox background + border
    c.fill_rect(tb_x, tb_y, tb_w, tb_h, colors::WHITE);
    c.rect(tb_x, tb_y, tb_w, tb_h, colors::BORDER);

    // Search text
    c.text(tb_x + 4, tb_y + (tb_h - system_font_height()) / 2, ws->searchQuery, colors::TEXT_COLOR);

    // ---- Search button ----
    int btn_x = tb_x + tb_w + 6;
    int btn_y = tb_y;
    int btn_w = 66;
    int btn_h = tb_h;
    c.button(btn_x, btn_y, btn_w, btn_h, "Search", colors::ACCENT, colors::WHITE, 0);

    // ---- Content area ----
    int content_y = WIKI_TOOLBAR_H + 1;
    int content_h = c.h - content_y;
    int wiki_sfh = system_font_height();
    int visibleLines = content_h / (wiki_sfh + 4);
    if (visibleLines < 1) visibleLines = 1;

    if (ws->mode == WikiState::FETCHING) {
        c.text(16, content_y + 16, "Loading...", Color::from_rgb(0x88, 0x88, 0x88));
    } else if (ws->mode == WikiState::WIKI_ERROR) {
        c.text(16, content_y + 16, ws->statusMsg, colors::CLOSE_BTN);
    } else if (ws->mode == WikiState::IDLE) {
        c.text(16, content_y + 16,
            "Type a topic and press Enter or click Search.",
            Color::from_rgb(0x88, 0x88, 0x88));
    } else if (ws->mode == WikiState::DONE && ws->lineCount > 0) {
        int y = content_y + 8;
        int lineH = wiki_sfh + 4;
        for (int i = ws->scrollY; i < ws->lineCount && y + wiki_sfh < c.h; i++) {
            WikiDisplayLine* dl = &ws->lines[i];
            if (dl->text[0] != '\0') {
                c.text(12, y, dl->text, dl->color);
            }
            y += lineH;
        }

        // ---- Scrollbar ----
        if (ws->lineCount > visibleLines) {
            int sb_x = c.w - WIKI_SCROLLBAR_W;
            int sb_y = content_y;
            int sb_h = content_h;

            c.fill_rect(sb_x, sb_y, WIKI_SCROLLBAR_W, sb_h, colors::SCROLLBAR_BG);

            // Thumb
            int maxScroll = ws->lineCount - visibleLines;
            if (maxScroll < 1) maxScroll = 1;
            int thumbH = (visibleLines * sb_h) / ws->lineCount;
            if (thumbH < 20) thumbH = 20;
            int thumbY = sb_y + (ws->scrollY * (sb_h - thumbH)) / maxScroll;

            c.fill_rect(sb_x + 2, thumbY, WIKI_SCROLLBAR_W - 4, thumbH, colors::SCROLLBAR_FG);
        }
    }
}

static void wiki_trigger_search(WikiState* ws) {
    if (ws->searchQuery[0] == '\0') return;
    if (ws->mode == WikiState::FETCHING) return;  // already fetching

    ws->lineCount = 0;
    ws->scrollY = 0;
    ws->respPos = 0;

    // Build args string: "-d <query>"
    static char args[512];
    args[0] = '-'; args[1] = 'd'; args[2] = ' ';
    int qi = 0;
    while (ws->searchQuery[qi] && qi < 500) {
        args[3 + qi] = ws->searchQuery[qi];
        qi++;
    }
    args[3 + qi] = '\0';

    ws->child_pid = zenith::spawn_redir("0:/os/wiki.elf", args);
    if (ws->child_pid <= 0) {
        snprintf(ws->statusMsg, sizeof(ws->statusMsg), "Error: could not start wiki process");
        ws->mode = WikiState::WIKI_ERROR;
        return;
    }

    ws->mode = WikiState::FETCHING;
}

static void wiki_on_mouse(Window* win, MouseEvent& ev) {
    WikiState* ws = (WikiState*)win->app_data;
    if (!ws) return;

    Rect cr = win->content_rect();
    int cw = cr.w;
    int local_x = ev.x - cr.x;
    int local_y = ev.y - cr.y;

    // Check search button click
    int tb_w = cw - 90;
    if (tb_w < 100) tb_w = 100;
    int btn_x = 8 + tb_w + 6;
    int btn_y = 6;
    int btn_w = 66;
    int btn_h = 24;

    if (ev.left_pressed()) {
        if (local_x >= btn_x && local_x < btn_x + btn_w &&
            local_y >= btn_y && local_y < btn_y + btn_h) {
            wiki_trigger_search(ws);
            return;
        }
    }

    // Scroll wheel
    if (ev.scroll != 0 && ws->mode == WikiState::DONE && ws->lineCount > 0) {
        int ch = cr.h;
        int content_h = ch - WIKI_TOOLBAR_H - 1;
        int visibleLines = content_h / (system_font_height() + 4);
        int maxScroll = ws->lineCount - visibleLines;
        if (maxScroll < 0) maxScroll = 0;

        ws->scrollY += ev.scroll * 3;
        if (ws->scrollY < 0) ws->scrollY = 0;
        if (ws->scrollY > maxScroll) ws->scrollY = maxScroll;
    }
}

static void wiki_on_key(Window* win, const Zenith::KeyEvent& key) {
    WikiState* ws = (WikiState*)win->app_data;
    if (!ws || !key.pressed) return;

    // Enter key triggers search
    if (key.ascii == '\n' || key.ascii == '\r') {
        wiki_trigger_search(ws);
        return;
    }

    // Page Up / Page Down / arrows
    if (ws->mode == WikiState::DONE && ws->lineCount > 0) {
        Rect cr = {0, 0, 0, 0};
        for (int i = 0; i < ws->ds->window_count; i++) {
            Window* w = &ws->ds->windows[i];
            if (w->app_data == ws) {
                cr = w->content_rect();
                break;
            }
        }
        int content_h = cr.h - WIKI_TOOLBAR_H - 1;
        int visibleLines = content_h / (system_font_height() + 4);
        if (visibleLines < 1) visibleLines = 1;
        int maxScroll = ws->lineCount - visibleLines;
        if (maxScroll < 0) maxScroll = 0;

        if (key.scancode == 0x49) { // Page Up
            ws->scrollY -= visibleLines;
            if (ws->scrollY < 0) ws->scrollY = 0;
            return;
        }
        if (key.scancode == 0x51) { // Page Down
            ws->scrollY += visibleLines;
            if (ws->scrollY > maxScroll) ws->scrollY = maxScroll;
            return;
        }
        if (key.scancode == 0x48) { // Up arrow
            if (ws->scrollY > 0) ws->scrollY--;
            return;
        }
        if (key.scancode == 0x50) { // Down arrow
            if (ws->scrollY < maxScroll) ws->scrollY++;
            return;
        }
        if (key.scancode == 0x47) { // Home
            ws->scrollY = 0;
            return;
        }
        if (key.scancode == 0x4F) { // End
            ws->scrollY = maxScroll;
            return;
        }
    }

    // Text input for search box
    if (key.ascii == '\b' || key.scancode == 0x0E) {
        int len = zenith::slen(ws->searchQuery);
        if (len > 0) ws->searchQuery[len - 1] = '\0';
    } else if (key.ascii >= 32 && key.ascii < 127) {
        int len = zenith::slen(ws->searchQuery);
        if (len < 254) {
            ws->searchQuery[len] = key.ascii;
            ws->searchQuery[len + 1] = '\0';
        }
    }
}

static void wiki_on_poll(Window* win) {
    WikiState* ws = (WikiState*)win->app_data;
    if (!ws) return;
    if (ws->mode != WikiState::FETCHING || ws->child_pid <= 0) return;

    // Non-blocking read from child process
    char buf[4096];
    int n = zenith::childio_read(ws->child_pid, buf, sizeof(buf));

    if (n > 0) {
        // Accumulate data, checking for EOT sentinel
        for (int i = 0; i < n && ws->respPos < WIKI_RESP_MAX - 1; i++) {
            if (buf[i] == '\x04') {
                // EOT: child is done, process the response
                ws->respBuf[ws->respPos] = '\0';
                ws->child_pid = -1;
                wiki_process_response(ws);
                return;
            }
            if (buf[i] == '\x01') {
                // Error sentinel from child
                ws->child_pid = -1;
                snprintf(ws->statusMsg, sizeof(ws->statusMsg),
                         "Article not found: %s", ws->searchQuery);
                ws->mode = WikiState::WIKI_ERROR;
                return;
            }
            ws->respBuf[ws->respPos++] = buf[i];
        }
    } else if (n < 0) {
        // Child process exited — process whatever we accumulated
        ws->child_pid = -1;
        if (ws->respPos > 0) {
            ws->respBuf[ws->respPos] = '\0';
            wiki_process_response(ws);
        } else {
            snprintf(ws->statusMsg, sizeof(ws->statusMsg),
                     "Error: fetch failed for \"%s\"", ws->searchQuery);
            ws->mode = WikiState::WIKI_ERROR;
        }
    }
}

static void wiki_on_close(Window* win) {
    WikiState* ws = (WikiState*)win->app_data;
    if (!ws) return;

    if (ws->lines) zenith::mfree(ws->lines);
    if (ws->respBuf) zenith::mfree(ws->respBuf);
    zenith::mfree(ws);
    win->app_data = nullptr;
}

// ============================================================================
// Wikipedia launcher
// ============================================================================

void open_wiki(DesktopState* ds) {
    int idx = desktop_create_window(ds, "Wikipedia", 100, 80, 600, 480);
    if (idx < 0) return;

    Window* win = &ds->windows[idx];
    WikiState* ws = (WikiState*)zenith::malloc(sizeof(WikiState));
    zenith::memset(ws, 0, sizeof(WikiState));
    ws->ds = ds;
    ws->mode = WikiState::IDLE;
    ws->searchQuery[0] = '\0';
    ws->scrollY = 0;
    ws->child_pid = -1;

    // Allocate display lines
    ws->lineCap = WIKI_MAX_LINES;
    ws->lines = (WikiDisplayLine*)zenith::malloc(ws->lineCap * sizeof(WikiDisplayLine));
    ws->lineCount = 0;

    // Allocate response buffer
    ws->respBuf = (char*)zenith::malloc(WIKI_RESP_MAX);
    ws->respPos = 0;

    ws->statusMsg[0] = '\0';

    win->app_data = ws;
    win->on_draw = wiki_on_draw;
    win->on_mouse = wiki_on_mouse;
    win->on_key = wiki_on_key;
    win->on_poll = wiki_on_poll;
    win->on_close = wiki_on_close;
}
