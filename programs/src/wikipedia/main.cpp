/*
    * main.cpp
    * ZenithOS Wikipedia GUI client - standalone Window Server process
    * Fetches articles via TLS (BearSSL), renders with Roboto TTF
    * Copyright (c) 2026 Daniel Hammer
*/

#include <zenith/syscall.h>
#include <zenith/string.h>
#include <zenith/heap.h>
#include <gui/gui.hpp>
#include <gui/truetype.hpp>

extern "C" {
#include <bearssl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
}

using namespace gui;
using namespace gui::colors;

// ============================================================================
// Constants
// ============================================================================

static constexpr int INIT_W       = 820;
static constexpr int INIT_H       = 580;
static constexpr int TOOLBAR_H    = 42;
static constexpr int SCROLLBAR_W  = 14;
static constexpr int FONT_SIZE    = 18;
static constexpr int TITLE_SIZE   = 32;
static constexpr int SECTION_SIZE = 24;
static constexpr int TEXT_PAD     = 16;
static constexpr int RESP_MAX     = 131072;
static constexpr int MAX_LINES    = 2000;

static const char WIKI_HOST[] = "en.wikipedia.org";

// ============================================================================
// Display line
// ============================================================================

struct WikiLine {
    char         text[256];
    Color        color;
    int          font_size;
    TrueTypeFont* font;   // which font to render with
};

// ============================================================================
// App state
// ============================================================================

enum class AppPhase { IDLE, LOADING, DONE, ERR };

static AppPhase      g_phase      = AppPhase::IDLE;
static char          g_query[256] = {};
static char          g_status[256] = {};
static int           g_scroll_y   = 0;
static int           g_line_count = 0;
static int           g_line_h     = 24;   // updated after font load
static int           g_win_w      = INIT_W;
static int           g_win_h      = INIT_H;
static char          g_title[512] = {};
static int           g_extract_len = 0;

// Large buffers — heap allocated in _start
static WikiLine*     g_lines      = nullptr;
static char*         g_resp_buf   = nullptr;
static char*         g_extract_buf = nullptr;

// Fonts
static TrueTypeFont* g_font       = nullptr;  // Roboto Medium
static TrueTypeFont* g_font_bold  = nullptr;  // Roboto Bold
static TrueTypeFont* g_font_serif = nullptr;  // NotoSerif SemiBold (headings)

// TLS state (lazy-init on first search)
static bool          g_tls_ready  = false;
static uint32_t      g_server_ip  = 0;

struct TrustAnchors {
    br_x509_trust_anchor* anchors;
    size_t count;
    size_t capacity;
};
static TrustAnchors g_tas = {nullptr, 0, 0};

// ============================================================================
// Pixel buffer helpers
// ============================================================================

static void px_fill(uint32_t* px, int bw, int x, int y, int w, int h, Color c) {
    uint32_t v = c.to_pixel();
    int x0 = x < 0 ? 0 : x,  y0 = y < 0 ? 0 : y;
    int x1 = x + w,           y1 = y + h;
    if (x1 > bw) x1 = bw;
    for (int row = y0; row < y1; row++)
        for (int col = x0; col < x1; col++)
            px[row * bw + col] = v;
}

static void px_hline(uint32_t* px, int bw, int x, int y, int len, Color c) {
    uint32_t v = c.to_pixel();
    int x1 = x + len;
    if (x < 0) x = 0;
    if (x1 > bw) x1 = bw;
    for (int col = x; col < x1; col++)
        px[y * bw + col] = v;
}

static void px_vline(uint32_t* px, int bw, int x, int y, int len, Color c) {
    uint32_t v = c.to_pixel();
    for (int row = y; row < y + len; row++)
        px[row * bw + x] = v;
}

static void px_rect_outline(uint32_t* px, int bw, int x, int y, int w, int h, Color c) {
    px_hline(px, bw, x,         y,         w, c);
    px_hline(px, bw, x,         y + h - 1, w, c);
    px_vline(px, bw, x,         y,         h, c);
    px_vline(px, bw, x + w - 1, y,         h, c);
}

// ============================================================================
// Trust anchor loading
// ============================================================================

struct DerAccum { unsigned char* data; size_t len, cap; };
struct DnAccum  { unsigned char* data; size_t len, cap; };

static void der_append(void* ctx, const void* buf, size_t len) {
    DerAccum* a = (DerAccum*)ctx;
    if (a->len + len > a->cap) {
        size_t nc = a->cap * 2;
        if (nc < a->len + len) nc = a->len + len + 4096;
        unsigned char* nb = (unsigned char*)malloc(nc);
        if (!nb) return;
        if (a->data) { memcpy(nb, a->data, a->len); free(a->data); }
        a->data = nb; a->cap = nc;
    }
    memcpy(a->data + a->len, buf, len);
    a->len += len;
}

static void dn_append(void* ctx, const void* buf, size_t len) {
    DnAccum* a = (DnAccum*)ctx;
    if (a->len + len > a->cap) {
        size_t nc = a->cap * 2;
        if (nc < a->len + len) nc = a->len + len + 256;
        unsigned char* nb = (unsigned char*)malloc(nc);
        if (!nb) return;
        if (a->data) { memcpy(nb, a->data, a->len); free(a->data); }
        a->data = nb; a->cap = nc;
    }
    memcpy(a->data + a->len, buf, len);
    a->len += len;
}

static void ta_add(TrustAnchors* tas, const br_x509_trust_anchor* ta) {
    if (tas->count >= tas->capacity) {
        size_t nc = tas->capacity == 0 ? 64 : tas->capacity * 2;
        br_x509_trust_anchor* na = (br_x509_trust_anchor*)malloc(nc * sizeof(*na));
        if (!na) return;
        if (tas->anchors) { memcpy(na, tas->anchors, tas->count * sizeof(*na)); free(tas->anchors); }
        tas->anchors = na; tas->capacity = nc;
    }
    tas->anchors[tas->count++] = *ta;
}

static bool process_cert_der(TrustAnchors* tas, const unsigned char* der, size_t der_len) {
    static br_x509_decoder_context dc;
    DnAccum dn = {nullptr, 0, 0};
    br_x509_decoder_init(&dc, dn_append, &dn);
    br_x509_decoder_push(&dc, der, der_len);
    br_x509_pkey* pk = br_x509_decoder_get_pkey(&dc);
    if (!pk) { if (dn.data) free(dn.data); return false; }

    br_x509_trust_anchor ta;
    memset(&ta, 0, sizeof(ta));
    ta.dn.data = dn.data; ta.dn.len = dn.len; ta.flags = 0;
    if (br_x509_decoder_isCA(&dc)) ta.flags |= BR_X509_TA_CA;

    switch (pk->key_type) {
    case BR_KEYTYPE_RSA:
        ta.pkey.key_type = BR_KEYTYPE_RSA;
        ta.pkey.key.rsa.nlen = pk->key.rsa.nlen;
        ta.pkey.key.rsa.n = (unsigned char*)malloc(pk->key.rsa.nlen);
        if (ta.pkey.key.rsa.n) memcpy(ta.pkey.key.rsa.n, pk->key.rsa.n, pk->key.rsa.nlen);
        ta.pkey.key.rsa.elen = pk->key.rsa.elen;
        ta.pkey.key.rsa.e = (unsigned char*)malloc(pk->key.rsa.elen);
        if (ta.pkey.key.rsa.e) memcpy(ta.pkey.key.rsa.e, pk->key.rsa.e, pk->key.rsa.elen);
        break;
    case BR_KEYTYPE_EC:
        ta.pkey.key_type = BR_KEYTYPE_EC;
        ta.pkey.key.ec.curve = pk->key.ec.curve;
        ta.pkey.key.ec.qlen = pk->key.ec.qlen;
        ta.pkey.key.ec.q = (unsigned char*)malloc(pk->key.ec.qlen);
        if (ta.pkey.key.ec.q) memcpy(ta.pkey.key.ec.q, pk->key.ec.q, pk->key.ec.qlen);
        break;
    default:
        if (dn.data) free(dn.data);
        return false;
    }
    ta_add(tas, &ta);
    return true;
}

static TrustAnchors load_trust_anchors() {
    TrustAnchors tas = {nullptr, 0, 0};
    int fh = zenith::open("0:/etc/ca-certificates.crt");
    if (fh < 0) return tas;
    uint64_t fsize = zenith::getsize(fh);
    if (fsize == 0 || fsize > 512 * 1024) { zenith::close(fh); return tas; }

    unsigned char* pem = (unsigned char*)malloc(fsize + 1);
    if (!pem) { zenith::close(fh); return tas; }
    zenith::read(fh, pem, 0, fsize);
    zenith::close(fh);
    pem[fsize] = 0;

    static br_pem_decoder_context pc;
    br_pem_decoder_init(&pc);
    DerAccum der = {nullptr, 0, 0};
    bool inCert = false;
    size_t offset = 0;

    while (offset < fsize) {
        size_t pushed = br_pem_decoder_push(&pc, pem + offset, fsize - offset);
        offset += pushed;
        int ev = br_pem_decoder_event(&pc);
        if (ev == BR_PEM_BEGIN_OBJ) {
            inCert = (strcmp(br_pem_decoder_name(&pc), "CERTIFICATE") == 0);
            br_pem_decoder_setdest(&pc, inCert ? der_append : nullptr, inCert ? &der : nullptr);
            if (inCert) der.len = 0;
        } else if (ev == BR_PEM_END_OBJ) {
            if (inCert && der.len > 0) process_cert_der(&tas, der.data, der.len);
            inCert = false;
        } else if (ev == BR_PEM_ERROR) {
            break;
        }
    }
    if (der.data) free(der.data);
    free(pem);
    return tas;
}

// ============================================================================
// BearSSL time
// ============================================================================

static void get_bearssl_time(uint32_t* days, uint32_t* seconds) {
    Zenith::DateTime dt;
    zenith::gettime(&dt);
    int y = dt.Year, m = dt.Month, d = dt.Day;
    uint32_t total = 365u * (uint32_t)y
        + (uint32_t)(y/4) - (uint32_t)(y/100) + (uint32_t)(y/400);
    const int md[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    for (int mo = 1; mo < m && mo <= 12; mo++) total += md[mo];
    if (y%4==0 && (y%100!=0 || y%400==0) && m > 2) total++;
    total += d - 1;
    *days = total;
    *seconds = (uint32_t)(dt.Hour*3600 + dt.Minute*60 + dt.Second);
}

// ============================================================================
// TLS I/O
// ============================================================================

static int tls_send_all(int fd, const unsigned char* data, size_t len) {
    size_t sent = 0;
    uint64_t deadline = zenith::get_milliseconds() + 15000;
    while (sent < len) {
        int r = zenith::send(fd, data + sent, (uint32_t)(len - sent));
        if (r > 0) { sent += r; deadline = zenith::get_milliseconds() + 15000; }
        else if (r < 0) return -1;
        else { if (zenith::get_milliseconds() >= deadline) return -1; zenith::sleep_ms(1); }
    }
    return (int)sent;
}

static int tls_recv_some(int fd, unsigned char* buf, size_t maxlen) {
    uint64_t deadline = zenith::get_milliseconds() + 15000;
    while (true) {
        int r = zenith::recv(fd, buf, (uint32_t)maxlen);
        if (r > 0) return r;
        if (r < 0) return -1;
        if (zenith::get_milliseconds() >= deadline) return -1;
        zenith::sleep_ms(1);
    }
}

static int tls_exchange(int fd, br_ssl_engine_context* eng,
                        const char* request, int reqLen,
                        char* respBuf, int respMax) {
    bool requestSent = false;
    int respLen = 0;
    uint64_t deadline = zenith::get_milliseconds() + 30000;

    while (true) {
        unsigned state = br_ssl_engine_current_state(eng);
        if (state & BR_SSL_CLOSED) {
            int err = br_ssl_engine_last_error(eng);
            if (err != BR_ERR_OK && err != BR_ERR_IO && respLen == 0) return -1;
            return respLen;
        }

        if (state & BR_SSL_SENDREC) {
            size_t len; unsigned char* buf = br_ssl_engine_sendrec_buf(eng, &len);
            int sent = tls_send_all(fd, buf, len);
            if (sent < 0) { br_ssl_engine_close(eng); return respLen > 0 ? respLen : -1; }
            br_ssl_engine_sendrec_ack(eng, len);
            deadline = zenith::get_milliseconds() + 30000; continue;
        }
        if (state & BR_SSL_RECVAPP) {
            size_t len; unsigned char* buf = br_ssl_engine_recvapp_buf(eng, &len);
            size_t toCopy = len;
            if (respLen + (int)toCopy > respMax - 1) toCopy = respMax - 1 - respLen;
            if (toCopy > 0) { memcpy(respBuf + respLen, buf, toCopy); respLen += toCopy; }
            br_ssl_engine_recvapp_ack(eng, len);
            deadline = zenith::get_milliseconds() + 30000; continue;
        }
        if ((state & BR_SSL_SENDAPP) && !requestSent) {
            size_t len; unsigned char* buf = br_ssl_engine_sendapp_buf(eng, &len);
            size_t toWrite = (size_t)reqLen;
            if (toWrite > len) toWrite = len;
            memcpy(buf, request, toWrite);
            br_ssl_engine_sendapp_ack(eng, toWrite);
            br_ssl_engine_flush(eng, 0);
            requestSent = true;
            deadline = zenith::get_milliseconds() + 30000; continue;
        }
        if (state & BR_SSL_RECVREC) {
            size_t len; unsigned char* buf = br_ssl_engine_recvrec_buf(eng, &len);
            int got = tls_recv_some(fd, buf, len);
            if (got < 0) { br_ssl_engine_close(eng); return respLen > 0 ? respLen : -1; }
            br_ssl_engine_recvrec_ack(eng, got);
            deadline = zenith::get_milliseconds() + 30000; continue;
        }
        if (zenith::get_milliseconds() >= deadline) return respLen > 0 ? respLen : -1;
        zenith::sleep_ms(1);
    }
}

static int wiki_fetch(const char* path, char* respBuf, int respMax) {
    int fd = zenith::socket(Zenith::SOCK_TCP);
    if (fd < 0) return -1;
    if (zenith::connect(fd, g_server_ip, 443) < 0) { zenith::closesocket(fd); return -1; }

    br_ssl_client_context* cc = (br_ssl_client_context*)malloc(sizeof(*cc));
    br_x509_minimal_context* xc = (br_x509_minimal_context*)malloc(sizeof(*xc));
    void* iobuf = malloc(BR_SSL_BUFSIZE_BIDI);
    if (!cc || !xc || !iobuf) {
        if (cc) free(cc); if (xc) free(xc); if (iobuf) free(iobuf);
        zenith::closesocket(fd); return -1;
    }

    br_ssl_client_init_full(cc, xc, g_tas.anchors, g_tas.count);
    uint32_t days, secs;
    get_bearssl_time(&days, &secs);
    br_x509_minimal_set_time(xc, days, secs);

    unsigned char seed[32];
    zenith::getrandom(seed, sizeof(seed));
    br_ssl_engine_set_buffer(&cc->eng, iobuf, BR_SSL_BUFSIZE_BIDI, 1);
    br_ssl_engine_inject_entropy(&cc->eng, seed, sizeof(seed));

    if (!br_ssl_client_reset(cc, WIKI_HOST, 0)) {
        zenith::closesocket(fd); free(cc); free(xc); free(iobuf); return -1;
    }

    static char request[2560];
    int reqLen = snprintf(request, sizeof(request),
        "GET %s HTTP/1.0\r\n"
        "Host: %s\r\n"
        "User-Agent: ZenithOS/1.0 wikipedia\r\n"
        "Accept: application/json\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, WIKI_HOST);

    int respLen = tls_exchange(fd, &cc->eng, request, reqLen, respBuf, respMax);
    zenith::closesocket(fd);
    free(cc); free(xc); free(iobuf);
    return respLen;
}

// ============================================================================
// HTTP parsing
// ============================================================================

static int find_header_end(const char* buf, int len) {
    for (int i = 0; i + 3 < len; i++)
        if (buf[i]=='\r' && buf[i+1]=='\n' && buf[i+2]=='\r' && buf[i+3]=='\n')
            return i + 4;
    return -1;
}

static int parse_status_code(const char* buf, int len) {
    int i = 0;
    while (i < len && buf[i] != ' ') i++;
    if (i >= len || i + 3 >= len) return -1;
    i++;
    if (buf[i] < '0' || buf[i] > '9') return -1;
    return (buf[i]-'0')*100 + (buf[i+1]-'0')*10 + (buf[i+2]-'0');
}

// ============================================================================
// URL encoding
// ============================================================================

static int url_encode_title(const char* in, char* out, int maxLen) {
    const char hex[] = "0123456789ABCDEF";
    int j = 0;
    for (int i = 0; in[i] && j < maxLen - 4; i++) {
        char c = in[i];
        if (c == ' ') {
            out[j++] = '_';
        } else if ((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||
                   c=='-'||c=='_'||c=='.'||c=='~'||c=='('||c==')'||c==',') {
            out[j++] = c;
        } else {
            out[j++] = '%';
            out[j++] = hex[(unsigned char)c >> 4];
            out[j++] = hex[(unsigned char)c & 0x0F];
        }
    }
    out[j] = '\0';
    return j;
}

// ============================================================================
// JSON string extraction
// ============================================================================

static int extract_json_string(const char* buf, int len, const char* key,
                                char* out, int maxOut) {
    int klen = (int)strlen(key);
    for (int i = 0; i < len - klen - 3; i++) {
        if (buf[i] != '"') continue;
        if (memcmp(buf + i + 1, key, klen) != 0) continue;
        if (buf[i + 1 + klen] != '"') continue;
        if (buf[i + 2 + klen] != ':') continue;

        int p = i + 3 + klen;
        while (p < len && (buf[p]==' ' || buf[p]=='\t')) p++;
        if (p >= len || buf[p] != '"') continue;
        p++;

        int j = 0;
        while (p < len && j < maxOut - 4) {
            if (buf[p] == '"') break;
            if (buf[p] == '\\' && p + 1 < len) {
                p++;
                switch (buf[p]) {
                case '"':  out[j++] = '"';  break;
                case '\\': out[j++] = '\\'; break;
                case 'n':  out[j++] = '\n'; break;
                case 'r':  break;
                case 't':  out[j++] = '\t'; break;
                case '/':  out[j++] = '/';  break;
                case 'u': {
                    if (p + 4 < len) {
                        unsigned val = 0;
                        for (int k = 1; k <= 4; k++) {
                            char h = buf[p + k]; val <<= 4;
                            if (h>='0'&&h<='9') val |= h-'0';
                            else if (h>='a'&&h<='f') val |= h-'a'+10;
                            else if (h>='A'&&h<='F') val |= h-'A'+10;
                        }
                        p += 4;
                        if (val < 128) out[j++] = (char)val;
                        else if (val==0x2013||val==0x2014) out[j++] = '-';
                        else if (val==0x2018||val==0x2019) out[j++] = '\'';
                        else if (val==0x201C||val==0x201D) out[j++] = '"';
                        else if (val==0x2026) { out[j++]='.'; out[j++]='.'; out[j++]='.'; }
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
// Display line building
// ============================================================================

static void add_line(const char* text, int len, Color color, int size, TrueTypeFont* font) {
    if (g_line_count >= MAX_LINES) return;
    WikiLine* l = &g_lines[g_line_count++];
    int copy = len < 255 ? len : 255;
    memcpy(l->text, text, copy);
    l->text[copy] = '\0';
    l->color     = color;
    l->font_size = size;
    l->font      = font;
}

static void add_empty_line() {
    if (g_line_count >= MAX_LINES) return;
    WikiLine* l = &g_lines[g_line_count++];
    l->text[0]   = '\0';
    l->color     = TEXT_COLOR;
    l->font_size = FONT_SIZE;
    l->font      = g_font;
}

// Word-wrap a text segment into display lines using pixel-width measurement.
static void wrap_text(TrueTypeFont* font, int size, const char* text, int textLen,
                      int max_px, Color color) {
    char  cur[256];
    int   cur_len = 0;
    const char* p   = text;
    const char* end = text + textLen;

    while (p < end) {
        while (p < end && *p == ' ') p++;
        if (p >= end) break;

        const char* word_start = p;
        while (p < end && *p != ' ') p++;
        int word_len = (int)(p - word_start);
        if (word_len <= 0) continue;

        // Build candidate: current line + space + word
        char test[260];
        int test_len = cur_len;
        memcpy(test, cur, cur_len);
        if (cur_len > 0) test[test_len++] = ' ';
        int copy = word_len < (int)(sizeof(test) - test_len - 1)
                 ? word_len : (int)(sizeof(test) - test_len - 1);
        memcpy(test + test_len, word_start, copy);
        test_len += copy;
        test[test_len] = '\0';

        int test_w = font->measure_text(test, size);
        if (test_w <= max_px || cur_len == 0) {
            // Fits — append to current line
            memcpy(cur, test, test_len + 1);
            cur_len = test_len;
        } else {
            // Emit current line and start fresh
            if (cur_len > 0) add_line(cur, cur_len, color, size, font);
            int wl = word_len < 254 ? word_len : 254;
            memcpy(cur, word_start, wl);
            cur_len = wl;
            cur[cur_len] = '\0';
        }
    }
    if (cur_len > 0) add_line(cur, cur_len, color, size, font);
}

static void build_display_lines(const char* title, const char* extract, int extractLen) {
    g_line_count = 0;
    g_scroll_y   = 0;

    // Max pixel width for text (accounting for left pad, right pad, scrollbar)
    int max_px = g_win_w - TEXT_PAD - SCROLLBAR_W - TEXT_PAD;

    // Title — large, serif, black
    if (title && title[0]) {
        TrueTypeFont* tf = g_font_serif ? g_font_serif : g_font;
        wrap_text(tf, TITLE_SIZE, title, (int)strlen(title), max_px, BLACK);
        add_empty_line();
    }

    // Process extract line-by-line
    const char* p   = extract;
    const char* end = extract + extractLen;

    while (p < end && g_line_count < MAX_LINES) {
        const char* line_start = p;
        while (p < end && *p != '\n') p++;
        int line_len = (int)(p - line_start);
        if (p < end) p++;  // consume '\n'

        if (line_len == 0) {
            add_empty_line();
            continue;
        }

        // Section header: == Title ==
        if (line_len >= 4 && line_start[0] == '=' && line_start[1] == '=') {
            int si = 0, ei = line_len;
            while (si < line_len && line_start[si] == '=') si++;
            while (si < line_len && line_start[si] == ' ') si++;
            while (ei > si && line_start[ei-1] == '=') ei--;
            while (ei > si && line_start[ei-1] == ' ') ei--;
            if (ei > si) {
                add_empty_line();
                TrueTypeFont* tf = g_font_serif ? g_font_serif : g_font;
                wrap_text(tf, SECTION_SIZE, line_start + si, ei - si, max_px, BLACK);
            }
            continue;
        }

        // Regular body text
        wrap_text(g_font, FONT_SIZE, line_start, line_len, max_px, TEXT_COLOR);
    }
}

// ============================================================================
// Network search (blocking)
// ============================================================================

static void do_search(const char* query) {
    // Lazy TLS/DNS init
    if (!g_tls_ready) {
        g_server_ip = zenith::resolve(WIKI_HOST);
        if (g_server_ip == 0) {
            snprintf(g_status, sizeof(g_status),
                     "Error: could not resolve en.wikipedia.org");
            g_phase = AppPhase::ERR; return;
        }
        g_tas = load_trust_anchors();
        if (g_tas.count == 0) {
            snprintf(g_status, sizeof(g_status), "Error: no CA certificates loaded");
            g_phase = AppPhase::ERR; return;
        }
        g_tls_ready = true;
    }

    static char encoded[1024];
    url_encode_title(query, encoded, sizeof(encoded));

    static char path[2048];
    snprintf(path, sizeof(path),
        "/w/api.php?action=query&format=json&formatversion=2"
        "&prop=extracts&explaintext=1&titles=%s", encoded);

    int respLen = wiki_fetch(path, g_resp_buf, RESP_MAX);
    if (respLen <= 0) {
        snprintf(g_status, sizeof(g_status), "Error: no response from Wikipedia");
        g_phase = AppPhase::ERR; return;
    }
    g_resp_buf[respLen] = '\0';

    int headerEnd = find_header_end(g_resp_buf, respLen);
    if (headerEnd < 0) {
        snprintf(g_status, sizeof(g_status), "Error: malformed HTTP response");
        g_phase = AppPhase::ERR; return;
    }

    int status      = parse_status_code(g_resp_buf, headerEnd);
    const char* body = g_resp_buf + headerEnd;
    int bodyLen     = respLen - headerEnd;

    if (status == 404) {
        snprintf(g_status, sizeof(g_status), "Article not found: %s", query);
        g_phase = AppPhase::ERR; return;
    }

    extract_json_string(body, bodyLen, "title", g_title, sizeof(g_title));
    g_extract_len = extract_json_string(body, bodyLen, "extract",
                                        g_extract_buf, RESP_MAX - 1);
    if (g_extract_len == 0) {
        snprintf(g_status, sizeof(g_status), "No content found for: %s", query);
        g_phase = AppPhase::ERR; return;
    }

    build_display_lines(g_title, g_extract_buf, g_extract_len);
    g_phase = AppPhase::DONE;
}

// ============================================================================
// Rendering
// ============================================================================

static void render(uint32_t* pixels) {
    static constexpr Color TOOLBAR_BG = Color::from_rgb(0xF5, 0xF5, 0xF5);
    static constexpr Color HINT_COLOR = Color::from_rgb(0x99, 0x99, 0x99);

    // Background
    px_fill(pixels, g_win_w, 0, 0, g_win_w, g_win_h, WINDOW_BG);

    // ---- Toolbar ----
    px_fill(pixels, g_win_w, 0, 0, g_win_w, TOOLBAR_H, TOOLBAR_BG);
    px_hline(pixels, g_win_w, 0, TOOLBAR_H, g_win_w, BORDER);

    // Search box geometry
    int sb_y = 8, sb_h = TOOLBAR_H - 16;
    int btn_w = 80, btn_gap = 8;
    int sb_x = 8;
    int sb_w = g_win_w - sb_x - btn_gap - btn_w - 8;
    if (sb_w < 80) sb_w = 80;

    px_fill(pixels, g_win_w, sb_x, sb_y, sb_w, sb_h, WHITE);
    px_rect_outline(pixels, g_win_w, sb_x, sb_y, sb_w, sb_h, BORDER);

    // Search box text + cursor
    if (g_font) {
        int ty = sb_y + (sb_h - FONT_SIZE) / 2;
        g_font->draw_to_buffer(pixels, g_win_w, g_win_h,
                               sb_x + 6, ty, g_query, TEXT_COLOR, FONT_SIZE);
        int qw = g_font->measure_text(g_query, FONT_SIZE);
        int cx = sb_x + 6 + qw + 1;
        if (cx < sb_x + sb_w - 4)
            px_vline(pixels, g_win_w, cx, ty + 1, FONT_SIZE - 2, TEXT_COLOR);
    }

    // Search button
    int btn_x = sb_x + sb_w + btn_gap;
    px_fill(pixels, g_win_w, btn_x, sb_y, btn_w, sb_h, ACCENT);
    if (g_font) {
        int stw = g_font->measure_text("Search", FONT_SIZE);
        g_font->draw_to_buffer(pixels, g_win_w, g_win_h,
                               btn_x + (btn_w - stw) / 2,
                               sb_y + (sb_h - FONT_SIZE) / 2,
                               "Search", WHITE, FONT_SIZE);
    }

    // ---- Content area ----
    int cy = TOOLBAR_H + 1;
    int ch = g_win_h - cy;
    if (!g_font) return;

    if (g_phase == AppPhase::IDLE) {
        g_font->draw_to_buffer(pixels, g_win_w, g_win_h,
            TEXT_PAD, cy + 16,
            "Type a topic and press Enter or click Search.",
            HINT_COLOR, FONT_SIZE);
    } else if (g_phase == AppPhase::LOADING) {
        g_font->draw_to_buffer(pixels, g_win_w, g_win_h,
            TEXT_PAD, cy + 16,
            "Searching Wikipedia...", HINT_COLOR, FONT_SIZE);
    } else if (g_phase == AppPhase::ERR) {
        g_font->draw_to_buffer(pixels, g_win_w, g_win_h,
            TEXT_PAD, cy + 16, g_status, CLOSE_BTN, FONT_SIZE);
    } else if (g_phase == AppPhase::DONE && g_line_count > 0) {
        int visible = ch / g_line_h;  // approximate using body line height
        int y       = cy + 8;

        for (int i = g_scroll_y; i < g_line_count && y < g_win_h; i++) {
            WikiLine& l  = g_lines[i];
            int lh = g_font->get_line_height(l.font_size) + 4;
            if (y + lh > g_win_h) break;
            if (l.text[0] != '\0') {
                l.font->draw_to_buffer(pixels, g_win_w, g_win_h,
                                       TEXT_PAD, y, l.text, l.color, l.font_size);
            }
            y += lh;
        }

        // Scrollbar
        if (g_line_count > visible) {
            int sbx = g_win_w - SCROLLBAR_W;
            px_fill(pixels, g_win_w, sbx, cy, SCROLLBAR_W, ch, SCROLLBAR_BG);
            int max_sc  = g_line_count - visible;
            int thumb_h = (visible * ch) / g_line_count;
            if (thumb_h < 20) thumb_h = 20;
            int thumb_y = cy + (g_scroll_y * (ch - thumb_h)) / (max_sc > 0 ? max_sc : 1);
            px_fill(pixels, g_win_w, sbx + 2, thumb_y,
                    SCROLLBAR_W - 4, thumb_h, SCROLLBAR_FG);
        }
    }
}

// ============================================================================
// Entry point
// ============================================================================

extern "C" void _start() {
    // Allocate large buffers from heap
    g_lines       = (WikiLine*)zenith::malloc(MAX_LINES * sizeof(WikiLine));
    g_resp_buf    = (char*)malloc(RESP_MAX + 1);
    g_extract_buf = (char*)malloc(RESP_MAX + 1);
    if (!g_lines || !g_resp_buf || !g_extract_buf) zenith::exit(1);

    // Load fonts
    auto load_font = [](const char* path) -> TrueTypeFont* {
        TrueTypeFont* f = (TrueTypeFont*)zenith::malloc(sizeof(TrueTypeFont));
        if (!f) return nullptr;
        zenith::memset(f, 0, sizeof(TrueTypeFont));
        if (!f->init(path)) { zenith::mfree(f); return nullptr; }
        return f;
    };
    g_font       = load_font("0:/fonts/Roboto-Medium.ttf");
    g_font_bold  = load_font("0:/fonts/Roboto-Bold.ttf");
    g_font_serif = load_font("0:/fonts/NotoSerif-SemiBold.ttf");
    if (!g_font) zenith::exit(1);

    g_line_h = g_font->get_line_height(FONT_SIZE) + 4;

    // Create window
    Zenith::WinCreateResult wres;
    if (zenith::win_create("Wikipedia", INIT_W, INIT_H, &wres) < 0 || wres.id < 0)
        zenith::exit(1);

    int      win_id = wres.id;
    uint32_t* pixels = (uint32_t*)(uintptr_t)wres.pixelVa;

    render(pixels);
    zenith::win_present(win_id);

    bool search_pending = false;

    while (true) {
        Zenith::WinEvent ev;
        int r = zenith::win_poll(win_id, &ev);

        if (r < 0) break;  // window closed / error

        if (r == 0) {
            // No event — idle at ~60 fps
            zenith::sleep_ms(16);
            render(pixels);
            zenith::win_present(win_id);
            continue;
        }

        // ---- Handle event ----
        if (ev.type == 3) break;  // close

        if (ev.type == 2) {
            int new_w = ev.resize.w;
            int new_h = ev.resize.h;
            if (new_w > 0 && new_h > 0 && (new_w != g_win_w || new_h != g_win_h)) {
                uint64_t new_va = zenith::win_resize(win_id, new_w, new_h);
                if (new_va != 0) {
                    pixels = (uint32_t*)(uintptr_t)new_va;
                    g_win_w = new_w;
                    g_win_h = new_h;
                    if (g_phase == AppPhase::DONE && g_line_count > 0) {
                        build_display_lines(g_title, g_extract_buf, g_extract_len);
                    }
                }
            }

        } else if (ev.type == 0 && ev.key.pressed) {
            uint8_t ascii = ev.key.ascii;
            uint8_t scan  = ev.key.scancode;

            if (ascii == '\n' || ascii == '\r') {
                search_pending = true;
            } else if (ascii == '\b' || scan == 0x0E) {
                int len = (int)strlen(g_query);
                if (len > 0) g_query[len - 1] = '\0';
            } else if (ascii >= 32 && ascii < 127) {
                int len = (int)strlen(g_query);
                if (len < 254) { g_query[len] = ascii; g_query[len + 1] = '\0'; }
            } else if (g_phase == AppPhase::DONE) {
                // Navigation keys
                int visible = (g_win_h - TOOLBAR_H - 1) / g_line_h;
                int max_sc  = g_line_count - visible;
                if (max_sc < 0) max_sc = 0;
                if      (scan == 0x48) { if (g_scroll_y > 0) g_scroll_y--; }
                else if (scan == 0x50) { if (g_scroll_y < max_sc) g_scroll_y++; }
                else if (scan == 0x49) { g_scroll_y -= visible; if (g_scroll_y < 0) g_scroll_y = 0; }
                else if (scan == 0x51) { g_scroll_y += visible; if (g_scroll_y > max_sc) g_scroll_y = max_sc; }
                else if (scan == 0x47) { g_scroll_y = 0; }
                else if (scan == 0x4F) { g_scroll_y = max_sc; }
            }

        } else if (ev.type == 1) {
            // Mouse
            int mx = ev.mouse.x, my = ev.mouse.y;
            bool just_clicked = (ev.mouse.buttons & 1) && !(ev.mouse.prev_buttons & 1);

            // Search button hit-test
            int sb_h  = TOOLBAR_H - 16;
            int btn_w = 80;
            int sb_w  = g_win_w - 8 - 8 - btn_w - 8;
            if (sb_w < 80) sb_w = 80;
            int btn_x = 8 + sb_w + 8;
            if (just_clicked && mx >= btn_x && mx < btn_x + btn_w &&
                my >= 8 && my < 8 + sb_h) {
                search_pending = true;
            }

            // Scroll wheel
            if (ev.mouse.scroll != 0 && g_phase == AppPhase::DONE) {
                int visible = (g_win_h - TOOLBAR_H - 1) / g_line_h;
                int max_sc  = g_line_count - visible;
                if (max_sc < 0) max_sc = 0;
                g_scroll_y += ev.mouse.scroll * 3;
                if (g_scroll_y < 0)       g_scroll_y = 0;
                if (g_scroll_y > max_sc)  g_scroll_y = max_sc;
            }
        }

        // Trigger search if requested and query is non-empty
        if (search_pending && g_query[0] != '\0') {
            search_pending = false;
            g_phase = AppPhase::LOADING;
            render(pixels);
            zenith::win_present(win_id);
            do_search(g_query);  // blocking
        }

        render(pixels);
        zenith::win_present(win_id);
    }

    zenith::win_destroy(win_id);
    zenith::exit(0);
}
