/*
    * main.cpp
    * Wikipedia client for ZenithOS (TLS 1.2 via BearSSL)
    * Interactive fullscreen pager with colored output
    * Usage: wiki <title>          Show article summary
    *        wiki -f <title>       Show full article
    *        wiki -s <query>       Search for articles
    * Copyright (c) 2025-2026 Daniel Hammer
*/

#include <zenith/syscall.h>
#include <zenith/string.h>

extern "C" {
#include <bearssl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
}

using zenith::skip_spaces;

static constexpr int RESP_MAX = 131072;  // 128 KB
static const char WIKI_HOST[] = "en.wikipedia.org";
static constexpr int MAX_LINES = 4096;
static constexpr int MAX_SEARCH_RESULTS = 10;

enum Mode { MODE_SUMMARY, MODE_FULL, MODE_SEARCH };
enum LineType { LINE_BLANK, LINE_TITLE, LINE_DESC, LINE_SECTION, LINE_BODY };

struct WikiLine {
    const char* text;
    int len;
    LineType type;
    int level;
};

// ---- Global state (loaded once, reused across fetches) ----

struct TrustAnchors {
    br_x509_trust_anchor* anchors;
    size_t count;
    size_t capacity;
};

static uint32_t g_serverIp = 0;
static TrustAnchors g_tas = {nullptr, 0, 0};

// ---- Screen buffer for flicker-free rendering ----

static constexpr int SB_SIZE = 32768;
static char g_sb[SB_SIZE];
static int g_sbPos = 0;

static void sb_reset() { g_sbPos = 0; }
static void sb_putc(char c) { if (g_sbPos < SB_SIZE - 1) g_sb[g_sbPos++] = c; }
static void sb_puts(const char* s) { while (*s && g_sbPos < SB_SIZE - 1) g_sb[g_sbPos++] = *s++; }
static void sb_flush() { g_sb[g_sbPos] = '\0'; zenith::print(g_sb); }

static int int_to_buf(char* buf, int n) {
    if (n <= 0) { buf[0] = '0'; return 1; }
    char tmp[12]; int i = 0;
    while (n > 0) { tmp[i++] = '0' + (n % 10); n /= 10; }
    for (int j = 0; j < i; j++) buf[j] = tmp[i - 1 - j];
    return i;
}

static void sb_cursor_to(int row, int col) {
    sb_puts("\033[");
    char tmp[12]; int n = int_to_buf(tmp, row);
    for (int i = 0; i < n; i++) sb_putc(tmp[i]);
    sb_putc(';');
    n = int_to_buf(tmp, col);
    for (int i = 0; i < n; i++) sb_putc(tmp[i]);
    sb_putc('H');
}

// ---- Trust anchor loading ----

struct DerAccum {
    unsigned char* data;
    size_t len;
    size_t cap;
};

static void der_append(void* ctx, const void* buf, size_t len) {
    DerAccum* a = (DerAccum*)ctx;
    if (a->len + len > a->cap) {
        size_t newcap = a->cap * 2;
        if (newcap < a->len + len) newcap = a->len + len + 4096;
        unsigned char* nb = (unsigned char*)malloc(newcap);
        if (!nb) return;
        if (a->data) {
            memcpy(nb, a->data, a->len);
            free(a->data);
        }
        a->data = nb;
        a->cap = newcap;
    }
    memcpy(a->data + a->len, buf, len);
    a->len += len;
}

struct DnAccum {
    unsigned char* data;
    size_t len;
    size_t cap;
};

static void dn_append(void* ctx, const void* buf, size_t len) {
    DnAccum* a = (DnAccum*)ctx;
    if (a->len + len > a->cap) {
        size_t newcap = a->cap * 2;
        if (newcap < a->len + len) newcap = a->len + len + 256;
        unsigned char* nb = (unsigned char*)malloc(newcap);
        if (!nb) return;
        if (a->data) {
            memcpy(nb, a->data, a->len);
            free(a->data);
        }
        a->data = nb;
        a->cap = newcap;
    }
    memcpy(a->data + a->len, buf, len);
    a->len += len;
}

static void ta_add(TrustAnchors* tas, const br_x509_trust_anchor* ta) {
    if (tas->count >= tas->capacity) {
        size_t newcap = tas->capacity == 0 ? 64 : tas->capacity * 2;
        br_x509_trust_anchor* na = (br_x509_trust_anchor*)malloc(
            newcap * sizeof(br_x509_trust_anchor));
        if (!na) return;
        if (tas->anchors) {
            memcpy(na, tas->anchors, tas->count * sizeof(br_x509_trust_anchor));
            free(tas->anchors);
        }
        tas->anchors = na;
        tas->capacity = newcap;
    }
    tas->anchors[tas->count++] = *ta;
}

static bool process_cert_der(TrustAnchors* tas, const unsigned char* der, size_t der_len) {
    static br_x509_decoder_context dc;  // ~2KB+, keep off stack
    DnAccum dn = {nullptr, 0, 0};

    br_x509_decoder_init(&dc, dn_append, &dn);
    br_x509_decoder_push(&dc, der, der_len);

    br_x509_pkey* pk = br_x509_decoder_get_pkey(&dc);
    if (!pk) {
        if (dn.data) free(dn.data);
        return false;
    }

    br_x509_trust_anchor ta;
    memset(&ta, 0, sizeof(ta));
    ta.dn.data = dn.data;
    ta.dn.len = dn.len;
    ta.flags = 0;
    if (br_x509_decoder_isCA(&dc))
        ta.flags |= BR_X509_TA_CA;

    switch (pk->key_type) {
    case BR_KEYTYPE_RSA: {
        ta.pkey.key_type = BR_KEYTYPE_RSA;
        ta.pkey.key.rsa.nlen = pk->key.rsa.nlen;
        ta.pkey.key.rsa.n = (unsigned char*)malloc(pk->key.rsa.nlen);
        if (ta.pkey.key.rsa.n) memcpy(ta.pkey.key.rsa.n, pk->key.rsa.n, pk->key.rsa.nlen);
        ta.pkey.key.rsa.elen = pk->key.rsa.elen;
        ta.pkey.key.rsa.e = (unsigned char*)malloc(pk->key.rsa.elen);
        if (ta.pkey.key.rsa.e) memcpy(ta.pkey.key.rsa.e, pk->key.rsa.e, pk->key.rsa.elen);
        break;
    }
    case BR_KEYTYPE_EC: {
        ta.pkey.key_type = BR_KEYTYPE_EC;
        ta.pkey.key.ec.curve = pk->key.ec.curve;
        ta.pkey.key.ec.qlen = pk->key.ec.qlen;
        ta.pkey.key.ec.q = (unsigned char*)malloc(pk->key.ec.qlen);
        if (ta.pkey.key.ec.q) memcpy(ta.pkey.key.ec.q, pk->key.ec.q, pk->key.ec.qlen);
        break;
    }
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
    if (fsize == 0 || fsize > 512 * 1024) {
        zenith::close(fh);
        return tas;
    }

    unsigned char* pem = (unsigned char*)malloc(fsize + 1);
    if (!pem) { zenith::close(fh); return tas; }

    zenith::read(fh, pem, 0, fsize);
    zenith::close(fh);
    pem[fsize] = 0;

    static br_pem_decoder_context pc;  // keep off stack
    br_pem_decoder_init(&pc);

    DerAccum der = {nullptr, 0, 0};
    bool inCert = false;

    size_t offset = 0;
    while (offset < fsize) {
        size_t pushed = br_pem_decoder_push(&pc, pem + offset, fsize - offset);
        offset += pushed;

        int event = br_pem_decoder_event(&pc);
        if (event == BR_PEM_BEGIN_OBJ) {
            const char* name = br_pem_decoder_name(&pc);
            inCert = (strcmp(name, "CERTIFICATE") == 0);
            if (inCert) {
                der.len = 0;
                br_pem_decoder_setdest(&pc, der_append, &der);
            } else {
                br_pem_decoder_setdest(&pc, nullptr, nullptr);
            }
        } else if (event == BR_PEM_END_OBJ) {
            if (inCert && der.len > 0)
                process_cert_der(&tas, der.data, der.len);
            inCert = false;
        } else if (event == BR_PEM_ERROR) {
            break;
        }
    }

    if (der.data) free(der.data);
    free(pem);
    return tas;
}

// ---- Time conversion for certificate validation ----

static void get_bearssl_time(uint32_t* days, uint32_t* seconds) {
    Zenith::DateTime dt;
    zenith::gettime(&dt);

    int y = dt.Year;
    int m = dt.Month;
    int d = dt.Day;

    uint32_t total_days = 365 * (uint32_t)y
        + (uint32_t)(y / 4)
        - (uint32_t)(y / 100)
        + (uint32_t)(y / 400);

    const int mdays[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (int mo = 1; mo < m && mo <= 12; mo++)
        total_days += mdays[mo];
    bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    if (leap && m > 2) total_days++;
    total_days += d - 1;

    *days = total_days;
    *seconds = (uint32_t)(dt.Hour * 3600 + dt.Minute * 60 + dt.Second);
}

// ---- TLS I/O ----

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
            if (err != BR_ERR_OK && err != BR_ERR_IO) {
                if (respLen == 0) return -1;
            }
            return respLen;
        }

        if (zenith::is_key_available()) {
            Zenith::KeyEvent ev;
            zenith::getkey(&ev);
            if (ev.pressed && ev.ctrl && ev.ascii == 'q') {
                br_ssl_engine_close(eng);
                return respLen > 0 ? respLen : -1;
            }
        }

        if (state & BR_SSL_SENDREC) {
            size_t len;
            unsigned char* buf = br_ssl_engine_sendrec_buf(eng, &len);
            int sent = tls_send_all(fd, buf, len);
            if (sent < 0) { br_ssl_engine_close(eng); return respLen > 0 ? respLen : -1; }
            br_ssl_engine_sendrec_ack(eng, len);
            deadline = zenith::get_milliseconds() + 30000;
            continue;
        }

        if (state & BR_SSL_RECVAPP) {
            size_t len;
            unsigned char* buf = br_ssl_engine_recvapp_buf(eng, &len);
            size_t toCopy = len;
            if (respLen + (int)toCopy > respMax - 1)
                toCopy = respMax - 1 - respLen;
            if (toCopy > 0) { memcpy(respBuf + respLen, buf, toCopy); respLen += toCopy; }
            br_ssl_engine_recvapp_ack(eng, len);
            deadline = zenith::get_milliseconds() + 30000;
            continue;
        }

        if ((state & BR_SSL_SENDAPP) && !requestSent) {
            size_t len;
            unsigned char* buf = br_ssl_engine_sendapp_buf(eng, &len);
            size_t toWrite = (size_t)reqLen;
            if (toWrite > len) toWrite = len;
            memcpy(buf, request, toWrite);
            br_ssl_engine_sendapp_ack(eng, toWrite);
            br_ssl_engine_flush(eng, 0);
            requestSent = true;
            deadline = zenith::get_milliseconds() + 30000;
            continue;
        }

        if (state & BR_SSL_RECVREC) {
            size_t len;
            unsigned char* buf = br_ssl_engine_recvrec_buf(eng, &len);
            int got = tls_recv_some(fd, buf, len);
            if (got < 0) { br_ssl_engine_close(eng); return respLen > 0 ? respLen : -1; }
            br_ssl_engine_recvrec_ack(eng, got);
            deadline = zenith::get_milliseconds() + 30000;
            continue;
        }

        if (zenith::get_milliseconds() >= deadline)
            return respLen > 0 ? respLen : -1;
        zenith::sleep_ms(1);
    }
}

// ---- HTTPS fetch wrapper (reusable for multiple requests) ----

static int wiki_fetch(const char* path, char* respBuf, int respMax) {
    int fd = zenith::socket(Zenith::SOCK_TCP);
    if (fd < 0) return -1;

    if (zenith::connect(fd, g_serverIp, 443) < 0) {
        zenith::closesocket(fd);
        return -1;
    }

    br_ssl_client_context* cc = (br_ssl_client_context*)malloc(sizeof(br_ssl_client_context));
    br_x509_minimal_context* xc = (br_x509_minimal_context*)malloc(sizeof(br_x509_minimal_context));
    void* iobuf = malloc(BR_SSL_BUFSIZE_BIDI);
    if (!cc || !xc || !iobuf) {
        zenith::closesocket(fd);
        return -1;
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
        zenith::closesocket(fd);
        free(cc); free(xc); free(iobuf);
        return -1;
    }

    static char request[2560];  // keep off stack
    int reqLen = snprintf(request, sizeof(request),
        "GET %s HTTP/1.0\r\n"
        "Host: %s\r\n"
        "User-Agent: ZenithOS/1.0 wiki\r\n"
        "Accept: application/json\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, WIKI_HOST);

    int respLen = tls_exchange(fd, &cc->eng, request, reqLen, respBuf, respMax);

    zenith::closesocket(fd);
    free(cc); free(xc); free(iobuf);
    return respLen;
}

// ---- HTTP response parsing ----

static int find_header_end(const char* buf, int len) {
    for (int i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i+1] == '\n' && buf[i+2] == '\r' && buf[i+3] == '\n')
            return i + 4;
    }
    return -1;
}

static int parse_status_code(const char* buf, int len) {
    int i = 0;
    while (i < len && buf[i] != ' ') i++;
    if (i >= len) return -1;
    i++;
    if (i + 2 >= len) return -1;
    if (buf[i] < '0' || buf[i] > '9') return -1;
    return (buf[i] - '0') * 100 + (buf[i+1] - '0') * 10 + (buf[i+2] - '0');
}

// ---- URL encoding ----

static int url_encode_title(const char* in, char* out, int maxLen) {
    const char hex[] = "0123456789ABCDEF";
    int j = 0;
    for (int i = 0; in[i] && j < maxLen - 4; i++) {
        char c = in[i];
        if (c == ' ') {
            out[j++] = '_';
        } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                   (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                   c == '.' || c == '~' || c == '(' || c == ')' || c == ',') {
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

static int url_encode_query(const char* in, char* out, int maxLen) {
    const char hex[] = "0123456789ABCDEF";
    int j = 0;
    for (int i = 0; in[i] && j < maxLen - 4; i++) {
        char c = in[i];
        if (c == ' ') {
            out[j++] = '+';
        } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                   (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                   c == '.' || c == '~') {
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

// ---- JSON string extraction ----

static int extract_json_string(const char* buf, int len, const char* key,
                                char* out, int maxOut) {
    int klen = (int)strlen(key);

    for (int i = 0; i < len - klen - 3; i++) {
        if (buf[i] != '"') continue;
        if (memcmp(buf + i + 1, key, klen) != 0) continue;
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

// ---- Display line building (word wrap + section detection) ----

// Word-wrap a single paragraph into the lines array. Returns lines added.
static int wrap_paragraph(const char* text, int textLen, int cols,
                          WikiLine* lines, int maxLines, LineType type) {
    if (maxLines <= 0 || textLen <= 0 || cols <= 0) return 0;
    int count = 0;
    const char* p = text;
    const char* end = text + textLen;

    while (p < end && count < maxLines) {
        while (p < end && *p == ' ') p++;
        if (p >= end) break;

        const char* lineStart = p;
        const char* lastSpace = nullptr;
        int col = 0;

        while (p < end && col < cols) {
            if (*p == ' ') lastSpace = p;
            p++;
            col++;
        }

        if (p >= end) {
            // Rest fits on one line
            lines[count].text = lineStart;
            lines[count].len = (int)(p - lineStart);
            lines[count].type = type;
            lines[count].level = 0;
            count++;
        } else if (lastSpace && lastSpace > lineStart) {
            // Wrap at last space
            lines[count].text = lineStart;
            lines[count].len = (int)(lastSpace - lineStart);
            lines[count].type = type;
            lines[count].level = 0;
            count++;
            p = lastSpace + 1;
        } else {
            // Force break (word longer than cols)
            lines[count].text = lineStart;
            lines[count].len = (int)(p - lineStart);
            lines[count].type = type;
            lines[count].level = 0;
            count++;
        }
    }
    return count;
}

// Build all display lines from article parts. Returns total line count.
static int build_lines(WikiLine* lines, int maxLines,
                       const char* title, const char* description,
                       const char* extract, int extractLen,
                       int cols, bool fullMode) {
    int n = 0;

    // Title
    if (title && title[0] && n < maxLines) {
        n += wrap_paragraph(title, (int)strlen(title), cols,
                            lines + n, maxLines - n, LINE_TITLE);
    }

    // Description
    if (description && description[0] && n < maxLines) {
        n += wrap_paragraph(description, (int)strlen(description), cols,
                            lines + n, maxLines - n, LINE_DESC);
    }

    // Blank separator
    if (n > 0 && n < maxLines) {
        lines[n].text = "";
        lines[n].len = 0;
        lines[n].type = LINE_BLANK;
        lines[n].level = 0;
        n++;
    }

    // Process extract line by line
    const char* p = extract;
    const char* end = extract + extractLen;

    while (p < end && n < maxLines) {
        const char* lineStart = p;
        while (p < end && *p != '\n') p++;
        int lineLen = (int)(p - lineStart);
        if (p < end) p++; // skip \n

        // Blank line
        if (lineLen == 0) {
            lines[n].text = "";
            lines[n].len = 0;
            lines[n].type = LINE_BLANK;
            lines[n].level = 0;
            n++;
            continue;
        }

        // Section header detection (== Title ==)
        if (fullMode && lineLen >= 4 && lineStart[0] == '=' && lineStart[1] == '=') {
            int level = 0;
            int si = 0;
            while (si < lineLen && lineStart[si] == '=') { level++; si++; }
            while (si < lineLen && lineStart[si] == ' ') si++;
            int ei = lineLen;
            while (ei > si && lineStart[ei - 1] == '=') ei--;
            while (ei > si && lineStart[ei - 1] == ' ') ei--;

            // Blank line before section for spacing
            if (n > 0 && n < maxLines) {
                lines[n].text = "";
                lines[n].len = 0;
                lines[n].type = LINE_BLANK;
                lines[n].level = 0;
                n++;
            }

            if (n < maxLines) {
                lines[n].text = lineStart + si;
                lines[n].len = ei - si;
                lines[n].type = LINE_SECTION;
                lines[n].level = level;
                n++;
            }
            continue;
        }

        // Regular text — word wrap
        n += wrap_paragraph(lineStart, lineLen, cols,
                            lines + n, maxLines - n, LINE_BODY);
    }

    return n;
}

// ---- Pager rendering ----

static void render_pager(WikiLine* lines, int totalLines, int scroll,
                         int rows, int cols, const char* statusTitle,
                         const char* modeLabel) {
    int contentRows = rows - 1;
    sb_reset();
    sb_puts("\033[?25l");

    for (int r = 0; r < contentRows; r++) {
        sb_cursor_to(r + 1, 1);
        sb_puts("\033[2K");

        int idx = scroll + r;
        if (idx < 0 || idx >= totalLines) continue;

        WikiLine& ln = lines[idx];
        if (ln.type == LINE_BLANK) continue;

        switch (ln.type) {
        case LINE_TITLE:
            sb_puts("\033[1;36m"); // bold cyan
            break;
        case LINE_DESC:
            sb_puts("\033[33m");   // yellow
            break;
        case LINE_SECTION: {
            int indent = (ln.level <= 2) ? 0 : (ln.level - 2) * 2;
            for (int i = 0; i < indent; i++) sb_putc(' ');
            sb_puts("\033[1;32m"); // bold green
            break;
        }
        default:
            break;
        }

        int maxW = cols;
        if (ln.type == LINE_SECTION && ln.level > 2)
            maxW -= (ln.level - 2) * 2;
        int printLen = ln.len;
        if (printLen > maxW) printLen = maxW;
        for (int c = 0; c < printLen; c++)
            sb_putc(ln.text[c]);

        if (ln.type != LINE_BODY)
            sb_puts("\033[0m");
    }

    // Status bar
    sb_cursor_to(rows, 1);
    sb_puts("\033[7m");

    int visCol = 0;
    // Helper: write string and track visible columns
    #define SB_STATUS(s) do { const char* _s = (s); while (*_s) { sb_putc(*_s++); visCol++; } } while(0)

    SB_STATUS(" Wikipedia ");
    sb_puts("\033[7;33m"); // yellow reverse
    SB_STATUS(modeLabel);
    sb_puts("\033[7;37m"); // white reverse
    SB_STATUS(" | ");

    // Truncated title
    int maxTitleLen = cols / 3;
    int titleLen = (int)strlen(statusTitle);
    if (titleLen > maxTitleLen && maxTitleLen > 3) {
        for (int i = 0; i < maxTitleLen - 3; i++) { sb_putc(statusTitle[i]); visCol++; }
        SB_STATUS("...");
    } else {
        SB_STATUS(statusTitle);
    }

    SB_STATUS(" | ");
    char numStr[32];
    int nLen = snprintf(numStr, sizeof(numStr), "%d/%d", scroll + 1, totalLines);
    for (int i = 0; i < nLen; i++) { sb_putc(numStr[i]); visCol++; }
    SB_STATUS(" | q:Quit j/k:Scroll Space/b:Page ");

    #undef SB_STATUS

    // Pad to fill row
    while (visCol < cols) { sb_putc(' '); visCol++; }
    sb_puts("\033[0m");

    sb_flush();
}

static void run_pager(WikiLine* lines, int totalLines, const char* title,
                      const char* modeLabel, bool useAltScreen) {
    int cols = 80, rows = 25;
    zenith::termsize(&cols, &rows);

    if (useAltScreen) {
        zenith::print("\033[?1049h");
        zenith::print("\033[?25l");
    }

    int scroll = 0;
    int maxScroll = totalLines - (rows - 1);
    if (maxScroll < 0) maxScroll = 0;

    render_pager(lines, totalLines, scroll, rows, cols, title, modeLabel);

    bool running = true;
    while (running) {
        while (!zenith::is_key_available()) zenith::yield();

        Zenith::KeyEvent ev;
        zenith::getkey(&ev);
        if (!ev.pressed) continue;

        int contentRows = rows - 1;

        if (ev.ascii == 'q' || (ev.ctrl && ev.ascii == 'q')) {
            running = false;
            break;
        }

        switch (ev.ascii) {
        case 'j': if (scroll < maxScroll) scroll++; break;
        case 'k': if (scroll > 0) scroll--; break;
        case ' ': scroll += contentRows; if (scroll > maxScroll) scroll = maxScroll; break;
        case 'b': scroll -= contentRows; if (scroll < 0) scroll = 0; break;
        case 'g': scroll = 0; break;
        case 'G': scroll = maxScroll; break;
        default:
            switch (ev.scancode) {
            case 0x48: if (scroll > 0) scroll--; break;           // Up
            case 0x50: if (scroll < maxScroll) scroll++; break;   // Down
            case 0x49: scroll -= contentRows; if (scroll < 0) scroll = 0; break; // PgUp
            case 0x51: scroll += contentRows; if (scroll > maxScroll) scroll = maxScroll; break; // PgDn
            case 0x47: scroll = 0; break;         // Home
            case 0x4F: scroll = maxScroll; break;  // End
            }
            break;
        }

        if (running)
            render_pager(lines, totalLines, scroll, rows, cols, title, modeLabel);
    }

    if (useAltScreen) {
        zenith::print("\033[?25h");
        zenith::print("\033[?1049l");
    }
}

// ---- Search results ----

static int parse_search_titles(const char* body, int bodyLen,
                               char titles[][256], int maxResults) {
    int brackets = 0;
    int start = -1;
    for (int i = 0; i < bodyLen; i++) {
        if (body[i] == '[') {
            brackets++;
            if (brackets == 2) { start = i + 1; break; }
        }
    }
    if (start < 0) return 0;

    int count = 0;
    int i = start;
    while (i < bodyLen && body[i] != ']' && count < maxResults) {
        while (i < bodyLen && (body[i] == ' ' || body[i] == ',' ||
               body[i] == '\n' || body[i] == '\r')) i++;
        if (i >= bodyLen || body[i] == ']') break;

        if (body[i] == '"') {
            i++;
            int j = 0;
            while (i < bodyLen && body[i] != '"' && j < 255) {
                if (body[i] == '\\' && i + 1 < bodyLen) {
                    i++;
                    titles[count][j++] = body[i];
                } else {
                    titles[count][j++] = body[i];
                }
                i++;
            }
            titles[count][j] = '\0';
            if (i < bodyLen) i++;
            count++;
        } else {
            i++;
        }
    }
    return count;
}

static void render_search(char titles[][256], int count, const char* query,
                          int rows, int cols) {
    sb_reset();
    sb_puts("\033[?25l");
    sb_puts("\033[2J"); // clear screen

    // Header
    sb_cursor_to(2, 3);
    sb_puts("\033[1;36mWikipedia\033[0m");
    sb_puts("\033[90m - The Free Encyclopedia\033[0m");

    // Search query
    sb_cursor_to(4, 3);
    sb_puts("\033[1mSearch results for \"\033[33m");
    sb_puts(query);
    sb_puts("\033[0;1m\":\033[0m");

    // Results
    int resultRow = 6;
    for (int i = 0; i < count; i++) {
        sb_cursor_to(resultRow + i, 3);
        sb_puts("\033[1;36m");
        char num[8];
        snprintf(num, sizeof(num), "%2d", i + 1);
        sb_puts(num);
        sb_puts("\033[0m  \033[1;37m");
        // Truncate long titles
        int tLen = (int)strlen(titles[i]);
        int maxT = cols - 10;
        if (tLen > maxT && maxT > 3) {
            for (int c = 0; c < maxT - 3; c++) sb_putc(titles[i][c]);
            sb_puts("...");
        } else {
            sb_puts(titles[i]);
        }
        sb_puts("\033[0m");
    }

    if (count == 0) {
        sb_cursor_to(resultRow, 3);
        sb_puts("\033[33m(no results found)\033[0m");
    }

    // Instructions
    sb_cursor_to(resultRow + count + 2, 3);
    sb_puts("\033[90mPress ");
    if (count > 0) {
        sb_puts("\033[0;1m1");
        if (count > 1) {
            sb_putc('-');
            if (count >= 10) sb_putc('0');
            else sb_putc('0' + count);
        }
        sb_puts("\033[0;90m to view article, ");
    }
    sb_puts("\033[0;1mq\033[0;90m to quit\033[0m");

    // Status bar
    sb_cursor_to(rows, 1);
    sb_puts("\033[7m");
    char statusStr[256];
    snprintf(statusStr, sizeof(statusStr),
        " Wikipedia Search | \"%s\" | %d result%s ",
        query, count, count == 1 ? "" : "s");
    sb_puts(statusStr);
    int sLen = (int)strlen(statusStr);
    for (int i = sLen; i < cols; i++) sb_putc(' ');
    sb_puts("\033[0m");

    sb_flush();
}

// Returns selected index (0-based), or -1 for quit
static int run_search(char titles[][256], int count, const char* query,
                      int rows, int cols) {
    render_search(titles, count, query, rows, cols);

    while (true) {
        while (!zenith::is_key_available()) zenith::yield();

        Zenith::KeyEvent ev;
        zenith::getkey(&ev);
        if (!ev.pressed) continue;

        if (ev.ascii == 'q' || (ev.ctrl && ev.ascii == 'q'))
            return -1;

        int sel = -1;
        if (ev.ascii >= '1' && ev.ascii <= '9') sel = ev.ascii - '1';
        else if (ev.ascii == '0') sel = 9;

        if (sel >= 0 && sel < count) return sel;
    }
}

// ---- Entry point ----

extern "C" void _start() {
    static char argbuf[1024];  // keep off stack (16KB limit)
    zenith::getargs(argbuf, sizeof(argbuf));
    const char* arg = skip_spaces(argbuf);

    if (*arg == '\0') {
        zenith::print("\033[1;36mwiki\033[0m - Wikipedia article viewer\n\n");
        zenith::print("Usage: wiki <title>          Show article summary\n");
        zenith::print("       wiki -f <title>       Show full article\n");
        zenith::print("       wiki -s <query>       Search for articles\n");
        zenith::print("\nExamples:\n");
        zenith::print("  wiki Linux\n");
        zenith::print("  wiki -f C programming language\n");
        zenith::print("  wiki -s operating system\n");
        zenith::exit(0);
    }

    // Parse mode flag
    Mode mode = MODE_SUMMARY;
    if (arg[0] == '-' && arg[1] == 'f' && (arg[2] == ' ' || arg[2] == '\0')) {
        mode = MODE_FULL;
        arg = skip_spaces(arg + 2);
    } else if (arg[0] == '-' && arg[1] == 's' && (arg[2] == ' ' || arg[2] == '\0')) {
        mode = MODE_SEARCH;
        arg = skip_spaces(arg + 2);
    }

    if (*arg == '\0') {
        zenith::print("\033[1;31mError:\033[0m no article title or search query\n");
        zenith::exit(1);
    }

    // Trim trailing spaces from query
    static char query[512];
    int qlen = 0;
    while (arg[qlen] && qlen < 511) { query[qlen] = arg[qlen]; qlen++; }
    while (qlen > 0 && query[qlen - 1] == ' ') qlen--;
    query[qlen] = '\0';

    // Initialize: resolve DNS and load certs
    zenith::print("\033[1;33mConnecting to Wikipedia...\033[0m\n");

    g_serverIp = zenith::resolve(WIKI_HOST);
    if (g_serverIp == 0) {
        zenith::print("\033[1;31mError:\033[0m could not resolve en.wikipedia.org\n");
        zenith::exit(1);
    }

    g_tas = load_trust_anchors();
    if (g_tas.count == 0) {
        zenith::print("\033[1;31mError:\033[0m no CA certificates loaded\n");
        zenith::exit(1);
    }

    // Allocate shared buffers
    char* respBuf = (char*)malloc(RESP_MAX);
    WikiLine* lines = (WikiLine*)malloc(MAX_LINES * sizeof(WikiLine));
    char* extractBuf = (char*)malloc(RESP_MAX);
    if (!respBuf || !lines || !extractBuf) {
        zenith::print("\033[1;31mError:\033[0m out of memory\n");
        zenith::exit(1);
    }

    if (mode == MODE_SEARCH) {
        // ---- Search mode ----
        static char path[2048], encoded[1024];
        url_encode_query(query, encoded, sizeof(encoded));
        snprintf(path, sizeof(path),
            "/w/api.php?action=opensearch&search=%s&limit=10&format=json",
            encoded);

        int respLen = wiki_fetch(path, respBuf, RESP_MAX);
        if (respLen <= 0) {
            zenith::print("\033[1;31mError:\033[0m no response from Wikipedia\n");
            zenith::exit(1);
        }
        respBuf[respLen] = '\0';

        int headerEnd = find_header_end(respBuf, respLen);
        if (headerEnd < 0) {
            zenith::print("\033[1;31mError:\033[0m malformed response\n");
            zenith::exit(1);
        }

        const char* body = respBuf + headerEnd;
        int bodyLen = respLen - headerEnd;

        static char titles[MAX_SEARCH_RESULTS][256];
        int titleCount = parse_search_titles(body, bodyLen, titles, MAX_SEARCH_RESULTS);

        if (titleCount == 0) {
            zenith::print("\033[33mNo results found for \"");
            zenith::print(query);
            zenith::print("\"\033[0m\n");
            zenith::exit(0);
        }

        int cols = 80, rows = 25;
        zenith::termsize(&cols, &rows);

        // Enter alternate screen for interactive search
        zenith::print("\033[?1049h");
        zenith::print("\033[?25l");

        bool searchRunning = true;
        while (searchRunning) {
            int sel = run_search(titles, titleCount, query, rows, cols);

            if (sel < 0) {
                searchRunning = false;
                break;
            }

            // Show loading message on search screen
            sb_reset();
            int infoRow = 6 + titleCount + 2;
            sb_cursor_to(infoRow, 3);
            sb_puts("\033[2K\033[1;33mFetching \"");
            sb_puts(titles[sel]);
            sb_puts("\"...\033[0m");
            sb_flush();

            // Fetch selected article summary
            static char articlePath[2048], articleEncoded[1024];
            url_encode_title(titles[sel], articleEncoded, sizeof(articleEncoded));
            snprintf(articlePath, sizeof(articlePath),
                "/api/rest_v1/page/summary/%s", articleEncoded);

            respLen = wiki_fetch(articlePath, respBuf, RESP_MAX);
            if (respLen <= 0) {
                sb_reset();
                sb_cursor_to(infoRow, 3);
                sb_puts("\033[2K\033[1;31mFetch failed. Press any key.\033[0m");
                sb_flush();
                while (!zenith::is_key_available()) zenith::yield();
                Zenith::KeyEvent ev; zenith::getkey(&ev);
                continue;
            }
            respBuf[respLen] = '\0';

            headerEnd = find_header_end(respBuf, respLen);
            if (headerEnd < 0) continue;

            int statusCode = parse_status_code(respBuf, headerEnd);
            body = respBuf + headerEnd;
            bodyLen = respLen - headerEnd;

            if (statusCode == 404) {
                sb_reset();
                sb_cursor_to(infoRow, 3);
                sb_puts("\033[2K\033[1;31mArticle not found. Press any key.\033[0m");
                sb_flush();
                while (!zenith::is_key_available()) zenith::yield();
                Zenith::KeyEvent ev; zenith::getkey(&ev);
                continue;
            }

            static char title[512], description[512];
            extract_json_string(body, bodyLen, "title", title, sizeof(title));
            extract_json_string(body, bodyLen, "description", description, sizeof(description));
            int extractLen = extract_json_string(body, bodyLen, "extract",
                                                  extractBuf, RESP_MAX - 1);

            if (extractLen > 0) {
                int totalLines = build_lines(lines, MAX_LINES,
                    title, description, extractBuf, extractLen, cols, false);
                // Run pager without alt screen (we're already in one)
                run_pager(lines, totalLines, title, "Summary", false);
            }
            // Loop back to re-render search results
        }

        // Exit alternate screen
        zenith::print("\033[?25h");
        zenith::print("\033[?1049l");

    } else {
        // ---- Summary or Full Article mode ----
        static char path[2048], encoded[1024];
        if (mode == MODE_SUMMARY) {
            url_encode_title(query, encoded, sizeof(encoded));
            snprintf(path, sizeof(path),
                "/api/rest_v1/page/summary/%s", encoded);
        } else {
            url_encode_title(query, encoded, sizeof(encoded));
            snprintf(path, sizeof(path),
                "/w/api.php?action=query&format=json&formatversion=2"
                "&prop=extracts&explaintext=1&titles=%s", encoded);
        }

        int respLen = wiki_fetch(path, respBuf, RESP_MAX);
        if (respLen <= 0) {
            zenith::print("\033[1;31mError:\033[0m no response from Wikipedia\n");
            zenith::exit(1);
        }
        respBuf[respLen] = '\0';

        int headerEnd = find_header_end(respBuf, respLen);
        if (headerEnd < 0) {
            zenith::print("\033[1;31mError:\033[0m malformed response\n");
            zenith::exit(1);
        }

        int statusCode = parse_status_code(respBuf, headerEnd);
        const char* body = respBuf + headerEnd;
        int bodyLen = respLen - headerEnd;

        if (statusCode == 404) {
            zenith::print("\033[1;31mArticle not found:\033[0m ");
            zenith::print(query);
            zenith::putchar('\n');
            zenith::exit(1);
        }

        static char title[512], description[512];
        extract_json_string(body, bodyLen, "title", title, sizeof(title));
        if (mode == MODE_SUMMARY)
            extract_json_string(body, bodyLen, "description", description, sizeof(description));
        else
            description[0] = '\0';

        int extractLen = extract_json_string(body, bodyLen, "extract",
                                              extractBuf, RESP_MAX - 1);

        if (extractLen == 0) {
            zenith::print("\033[1;31mArticle not found:\033[0m ");
            zenith::print(query);
            zenith::putchar('\n');
            zenith::exit(1);
        }

        int cols = 80;
        zenith::termsize(&cols, nullptr);

        int totalLines = build_lines(lines, MAX_LINES,
            title, description, extractBuf, extractLen,
            cols, mode == MODE_FULL);

        const char* modeLabel = (mode == MODE_FULL) ? "Full Article" : "Summary";
        run_pager(lines, totalLines, title, modeLabel, true);
    }

    zenith::exit(0);
}
