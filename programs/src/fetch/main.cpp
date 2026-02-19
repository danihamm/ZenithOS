/*
    * main.cpp
    * HTTP/HTTPS client for ZenithOS (TLS 1.2 via BearSSL)
    * Usage: fetch [-v] <url>
    *        fetch [-v] <host> <port> [path]    (legacy mode, plain HTTP)
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

// ---- IP/port parsing ----

static bool parse_ip(const char* s, uint32_t* out) {
    uint32_t octets[4];
    int idx = 0;
    uint32_t val = 0;
    bool hasDigit = false;
    for (int i = 0; ; i++) {
        char c = s[i];
        if (c >= '0' && c <= '9') {
            val = val * 10 + (c - '0');
            if (val > 255) return false;
            hasDigit = true;
        } else if (c == '.' || c == '\0') {
            if (!hasDigit || idx >= 4) return false;
            octets[idx++] = val;
            val = 0; hasDigit = false;
            if (c == '\0') break;
        } else return false;
    }
    if (idx != 4) return false;
    *out = octets[0] | (octets[1] << 8) | (octets[2] << 16) | (octets[3] << 24);
    return true;
}

static bool parse_uint16(const char* s, uint16_t* out) {
    uint32_t val = 0;
    if (*s == '\0') return false;
    while (*s) {
        if (*s < '0' || *s > '9') return false;
        val = val * 10 + (*s - '0');
        if (val > 65535) return false;
        s++;
    }
    *out = (uint16_t)val;
    return true;
}

static void format_ip(char* buf, uint32_t ip) {
    snprintf(buf, 32, "%u.%u.%u.%u",
        ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
}

// ---- URL parser ----

struct ParsedUrl {
    char host[256];
    char path[512];
    uint16_t port;
    bool https;
    bool valid;
};

static ParsedUrl parse_url(const char* url) {
    ParsedUrl u;
    memset(&u, 0, sizeof(u));
    u.path[0] = '/'; u.path[1] = '\0';

    // Check scheme
    if (strncmp(url, "https://", 8) == 0) {
        u.https = true;
        u.port = 443;
        url += 8;
    } else if (strncmp(url, "http://", 7) == 0) {
        u.https = false;
        u.port = 80;
        url += 7;
    } else {
        u.valid = false;
        return u;
    }

    // Parse host (until '/', ':', or end)
    int i = 0;
    while (url[i] && url[i] != '/' && url[i] != ':' && i < 255) {
        u.host[i] = url[i];
        i++;
    }
    u.host[i] = '\0';
    url += i;

    // Optional port
    if (*url == ':') {
        url++;
        uint32_t p = 0;
        while (*url >= '0' && *url <= '9') {
            p = p * 10 + (*url - '0');
            url++;
        }
        if (p > 0 && p <= 65535) u.port = (uint16_t)p;
    }

    // Path
    if (*url == '/') {
        int j = 0;
        while (url[j] && j < 511) {
            u.path[j] = url[j];
            j++;
        }
        u.path[j] = '\0';
    }

    u.valid = (u.host[0] != '\0');
    return u;
}

// ---- HTTP response parser ----

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

static void parse_status_text(const char* buf, int len, char* out, int outMax) {
    int i = 0;
    while (i < len && buf[i] != ' ') i++;
    i++;
    while (i < len && buf[i] != ' ') i++;
    i++;
    int j = 0;
    while (i < len && buf[i] != '\r' && buf[i] != '\n' && j < outMax - 1)
        out[j++] = buf[i++];
    out[j] = '\0';
}

// ---- Trust anchor loading ----

struct TrustAnchors {
    br_x509_trust_anchor* anchors;
    size_t count;
    size_t capacity;
};

// Accumulate DER-decoded certificate data
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

// Accumulate DN data from X.509 decoder
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

// Process a single DER certificate into a trust anchor
static bool process_cert_der(TrustAnchors* tas, const unsigned char* der, size_t der_len) {
    br_x509_decoder_context dc;
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

    // Copy DN
    ta.dn.data = dn.data;
    ta.dn.len = dn.len;

    ta.flags = 0;
    if (br_x509_decoder_isCA(&dc))
        ta.flags |= BR_X509_TA_CA;

    // Deep-copy public key data
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

static TrustAnchors load_trust_anchors(bool verbose) {
    TrustAnchors tas = {nullptr, 0, 0};

    int fh = zenith::open("0:/etc/ca-certificates.crt");
    if (fh < 0) {
        printf("Warning: could not open CA certificate bundle\n");
        return tas;
    }

    uint64_t fsize = zenith::getsize(fh);
    if (fsize == 0 || fsize > 512 * 1024) {
        zenith::close(fh);
        printf("Warning: CA cert file invalid size\n");
        return tas;
    }

    unsigned char* pem = (unsigned char*)malloc(fsize + 1);
    if (!pem) {
        zenith::close(fh);
        printf("Warning: out of memory loading CA certs\n");
        return tas;
    }

    zenith::read(fh, pem, 0, fsize);
    zenith::close(fh);
    pem[fsize] = 0;

    // Parse PEM -> DER certificates -> trust anchors
    br_pem_decoder_context pc;
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
            if (inCert && der.len > 0) {
                process_cert_der(&tas, der.data, der.len);
            }
            inCert = false;
        } else if (event == BR_PEM_ERROR) {
            break;
        }
    }

    if (der.data) free(der.data);
    free(pem);

    if (verbose) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Loaded %u trust anchors\n", (unsigned)tas.count);
        zenith::print(msg);
    }

    return tas;
}

// ---- Time conversion for certificate validation ----

// Returns days since January 1, 0 AD (Gregorian) and seconds within the day.
// This is the format BearSSL's br_x509_minimal_set_time() expects.
static void get_bearssl_time(uint32_t* days, uint32_t* seconds) {
    Zenith::DateTime dt;
    zenith::gettime(&dt);

    int y = dt.Year;
    int m = dt.Month;
    int d = dt.Day;

    // Days from year 0 to start of year y (Gregorian proleptic calendar)
    uint32_t total_days = 365 * (uint32_t)y
        + (uint32_t)(y / 4)
        - (uint32_t)(y / 100)
        + (uint32_t)(y / 400);

    // Add days for completed months in this year
    const int mdays[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (int mo = 1; mo < m && mo <= 12; mo++) {
        total_days += mdays[mo];
    }
    // Leap day for this year if we're past February
    bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    if (leap && m > 2) total_days++;

    total_days += d - 1;

    *days = total_days;
    *seconds = (uint32_t)(dt.Hour * 3600 + dt.Minute * 60 + dt.Second);
}

// ---- TLS I/O loop ----

static int tls_send_all(int fd, const unsigned char* data, size_t len) {
    size_t sent = 0;
    uint64_t deadline = zenith::get_milliseconds() + 15000;
    while (sent < len) {
        int r = zenith::send(fd, data + sent, (uint32_t)(len - sent));
        if (r > 0) {
            sent += r;
            deadline = zenith::get_milliseconds() + 15000;
        } else if (r < 0) {
            return -1;
        } else {
            if (zenith::get_milliseconds() >= deadline) return -1;
            zenith::sleep_ms(1);
        }
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

// Run BearSSL I/O loop until handshake + app data exchange is done
// Returns: number of response bytes in respBuf, or -1 on error
static int tls_exchange(int fd, br_ssl_engine_context* eng,
                        const char* request, int reqLen,
                        char* respBuf, int respMax, bool verbose) {
    bool requestSent = false;
    int respLen = 0;
    uint64_t deadline = zenith::get_milliseconds() + 30000;

    while (true) {
        unsigned state = br_ssl_engine_current_state(eng);

        if (state & BR_SSL_CLOSED) {
            int err = br_ssl_engine_last_error(eng);
            if (err != BR_ERR_OK && err != BR_ERR_IO) {
                char msg[64];
                snprintf(msg, sizeof(msg), "TLS error: %d\n", err);
                zenith::print(msg);
                if (respLen == 0) return -1;
            }
            return respLen;
        }

        // Check for keyboard abort
        if (zenith::is_key_available()) {
            Zenith::KeyEvent ev;
            zenith::getkey(&ev);
            if (ev.pressed && ev.ctrl && ev.ascii == 'q') {
                br_ssl_engine_close(eng);
                return respLen > 0 ? respLen : -1;
            }
        }

        // Send record data to network
        if (state & BR_SSL_SENDREC) {
            size_t len;
            unsigned char* buf = br_ssl_engine_sendrec_buf(eng, &len);
            int sent = tls_send_all(fd, buf, len);
            if (sent < 0) {
                br_ssl_engine_close(eng);
                return respLen > 0 ? respLen : -1;
            }
            br_ssl_engine_sendrec_ack(eng, len);
            deadline = zenith::get_milliseconds() + 30000;
            continue;
        }

        // Read application data from TLS
        if (state & BR_SSL_RECVAPP) {
            size_t len;
            unsigned char* buf = br_ssl_engine_recvapp_buf(eng, &len);
            size_t toCopy = len;
            if (respLen + (int)toCopy > respMax - 1)
                toCopy = respMax - 1 - respLen;
            if (toCopy > 0) {
                memcpy(respBuf + respLen, buf, toCopy);
                respLen += toCopy;
            }
            br_ssl_engine_recvapp_ack(eng, len);
            deadline = zenith::get_milliseconds() + 30000;
            continue;
        }

        // Send application data (HTTP request) into TLS
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

        // Receive record data from network
        if (state & BR_SSL_RECVREC) {
            size_t len;
            unsigned char* buf = br_ssl_engine_recvrec_buf(eng, &len);
            int got = tls_recv_some(fd, buf, len);
            if (got < 0) {
                br_ssl_engine_close(eng);
                return respLen > 0 ? respLen : -1;
            }
            br_ssl_engine_recvrec_ack(eng, got);
            deadline = zenith::get_milliseconds() + 30000;
            continue;
        }

        // Nothing actionable — wait
        if (zenith::get_milliseconds() >= deadline) {
            return respLen > 0 ? respLen : -1;
        }
        zenith::sleep_ms(1);
    }
}

// ---- Plain HTTP exchange (no TLS) ----

static int plain_http_exchange(int fd, const char* request, int reqLen,
                               char* respBuf, int respMax) {
    // Send request
    int sent = 0;
    uint64_t deadline = zenith::get_milliseconds() + 15000;
    while (sent < reqLen) {
        int r = zenith::send(fd, request + sent, reqLen - sent);
        if (r > 0) { sent += r; deadline = zenith::get_milliseconds() + 15000; }
        else if (r < 0) return -1;
        else {
            if (zenith::get_milliseconds() >= deadline) return -1;
            zenith::sleep_ms(1);
        }
    }

    // Receive response
    int respLen = 0;
    deadline = zenith::get_milliseconds() + 15000;
    while (respLen < respMax - 1) {
        if (zenith::is_key_available()) {
            Zenith::KeyEvent ev;
            zenith::getkey(&ev);
            if (ev.pressed && ev.ctrl && ev.ascii == 'q') return -2; // aborted
        }

        int r = zenith::recv(fd, respBuf + respLen, respMax - 1 - respLen);
        if (r > 0) { respLen += r; deadline = zenith::get_milliseconds() + 15000; }
        else if (r < 0) break;
        else {
            if (zenith::get_milliseconds() >= deadline) break;
            zenith::sleep_ms(1);
        }
    }
    return respLen;
}

// ---- Print response body ----

static void print_response(const char* respBuf, int respLen, bool verbose) {
    if (respLen <= 0) {
        zenith::print("Error: empty response\n");
        return;
    }

    int headerEnd = find_header_end(respBuf, respLen);
    if (headerEnd < 0) {
        zenith::print("Warning: malformed response (no header boundary)\n\n");
        // Print raw
        char chunk[512];
        int printed = 0;
        while (printed < respLen) {
            int n = respLen - printed;
            if (n > 511) n = 511;
            memcpy(chunk, respBuf + printed, n);
            chunk[n] = '\0';
            zenith::print(chunk);
            printed += n;
        }
        zenith::putchar('\n');
        return;
    }

    int statusCode = parse_status_code(respBuf, headerEnd);
    char statusText[64];
    parse_status_text(respBuf, headerEnd, statusText, sizeof(statusText));
    int bodyLen = respLen - headerEnd;

    if (verbose) {
        char msg[256];
        snprintf(msg, sizeof(msg), "HTTP %d %s (%d bytes)\n\n", statusCode, statusText, bodyLen);
        zenith::print(msg);
    }

    if (bodyLen > 0) {
        const char* body = respBuf + headerEnd;
        char chunk[512];
        int printed = 0;
        while (printed < bodyLen) {
            int n = bodyLen - printed;
            if (n > 511) n = 511;
            memcpy(chunk, body + printed, n);
            chunk[n] = '\0';
            zenith::print(chunk);
            printed += n;
        }
        zenith::putchar('\n');
    }
}

// ---- Main ----

extern "C" void _start() {
    char argbuf[1024];
    zenith::getargs(argbuf, sizeof(argbuf));
    const char* arg = skip_spaces(argbuf);

    if (*arg == '\0') {
        zenith::print("Usage: fetch [-v] <url>\n");
        zenith::print("       fetch [-v] <host> <port> [path]\n");
        zenith::print("\n");
        zenith::print("  -v  Verbose output (show connection info and headers)\n");
        zenith::print("\n");
        zenith::print("Examples:\n");
        zenith::print("  fetch https://icanhazip.com\n");
        zenith::print("  fetch http://example.com/index.html\n");
        zenith::print("  fetch -v https://example.com\n");
        zenith::print("  fetch 10.0.68.1 80 /\n");
        zenith::exit(0);
    }

    // Check for -v flag
    bool verbose = false;
    if (arg[0] == '-' && arg[1] == 'v' && (arg[2] == ' ' || arg[2] == '\0')) {
        verbose = true;
        arg = skip_spaces(arg + 2);
    }

    // Determine mode: URL mode (starts with http:// or https://) vs legacy mode
    bool urlMode = (strncmp(arg, "http://", 7) == 0 || strncmp(arg, "https://", 8) == 0);

    char hostStr[256];
    char path[512];
    uint16_t port;
    bool useHttps = false;

    if (urlMode) {
        ParsedUrl url = parse_url(arg);
        if (!url.valid) {
            zenith::print("Error: invalid URL\n");
            zenith::exit(1);
        }
        strcpy(hostStr, url.host);
        strcpy(path, url.path);
        port = url.port;
        useHttps = url.https;
    } else {
        // Legacy mode: <host> <port> [path]
        int i = 0;
        while (arg[i] && arg[i] != ' ' && i < 255) { hostStr[i] = arg[i]; i++; }
        hostStr[i] = '\0';
        arg = skip_spaces(arg + i);

        char portStr[16];
        i = 0;
        while (arg[i] && arg[i] != ' ' && i < 15) { portStr[i] = arg[i]; i++; }
        portStr[i] = '\0';
        arg = skip_spaces(arg + i);

        if (!parse_uint16(portStr, &port)) {
            zenith::print("Invalid port: ");
            zenith::print(portStr);
            zenith::putchar('\n');
            zenith::exit(1);
        }

        if (*arg) {
            i = 0;
            while (arg[i] && i < 511) { path[i] = arg[i]; i++; }
            path[i] = '\0';
        } else {
            path[0] = '/'; path[1] = '\0';
        }
    }

    // Resolve host to IP
    uint32_t serverIp;
    if (!parse_ip(hostStr, &serverIp)) {
        serverIp = zenith::resolve(hostStr);
        if (serverIp == 0) {
            zenith::print("Error: could not resolve ");
            zenith::print(hostStr);
            zenith::putchar('\n');
            zenith::exit(1);
        }
    }

    char ipStr[32];
    format_ip(ipStr, serverIp);

    if (verbose) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Connecting to %s:%d (%s)...\n",
            hostStr, (int)port, useHttps ? "HTTPS" : "HTTP");
        zenith::print(msg);
    }

    // Create and connect socket
    int fd = zenith::socket(Zenith::SOCK_TCP);
    if (fd < 0) {
        zenith::print("Error: failed to create socket\n");
        zenith::exit(1);
    }

    if (zenith::connect(fd, serverIp, port) < 0) {
        zenith::print("Error: connection failed\n");
        zenith::closesocket(fd);
        zenith::exit(1);
    }

    // Build HTTP request
    char request[1024];
    int reqLen = snprintf(request, sizeof(request),
        "GET %s HTTP/1.0\r\n"
        "Host: %s\r\n"
        "User-Agent: ZenithOS/1.0\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, hostStr);

    if (verbose) {
        char msg[128];
        snprintf(msg, sizeof(msg), "GET %s\n", path);
        zenith::print(msg);
    }

    // Allocate response buffer on heap (stack is only 16 KB)
    static constexpr int RESP_MAX = 65536;
    char* respBuf = (char*)malloc(RESP_MAX);
    if (!respBuf) {
        zenith::print("Error: out of memory\n");
        zenith::closesocket(fd);
        zenith::exit(1);
    }

    int respLen;

    if (useHttps) {
        // ---- TLS handshake and exchange ----

        // Load trust anchors
        TrustAnchors tas = load_trust_anchors(verbose);
        if (tas.count == 0) {
            zenith::print("Error: no trust anchors loaded\n");
            free(respBuf);
            zenith::closesocket(fd);
            zenith::exit(1);
        }

        // Initialize BearSSL client
        // Allocate contexts on heap to avoid stack overflow
        br_ssl_client_context* cc = (br_ssl_client_context*)malloc(sizeof(br_ssl_client_context));
        br_x509_minimal_context* xc = (br_x509_minimal_context*)malloc(sizeof(br_x509_minimal_context));
        if (!cc || !xc) {
            zenith::print("Error: out of memory for TLS context\n");
            free(respBuf);
            zenith::closesocket(fd);
            zenith::exit(1);
        }

        br_ssl_client_init_full(cc, xc, tas.anchors, tas.count);

        // Set time for certificate validation
        uint32_t days, secs;
        get_bearssl_time(&days, &secs);
        br_x509_minimal_set_time(xc, days, secs);

        if (verbose) {
            Zenith::DateTime dt;
            zenith::gettime(&dt);
            char tmsg[128];
            snprintf(tmsg, sizeof(tmsg), "System time: %u-%02u-%02u %02u:%02u:%02u (days=%u secs=%u)\n",
                (unsigned)dt.Year, (unsigned)dt.Month, (unsigned)dt.Day,
                (unsigned)dt.Hour, (unsigned)dt.Minute, (unsigned)dt.Second,
                (unsigned)days, (unsigned)secs);
            zenith::print(tmsg);
        }

        // Seed the PRNG with RDRAND entropy
        unsigned char seed[32];
        zenith::getrandom(seed, sizeof(seed));
        br_ssl_engine_set_buffer(&cc->eng, malloc(BR_SSL_BUFSIZE_BIDI),
                                 BR_SSL_BUFSIZE_BIDI, 1);

        // Inject entropy
        br_ssl_engine_inject_entropy(&cc->eng, seed, sizeof(seed));

        // Reset client with server name for SNI
        if (!br_ssl_client_reset(cc, hostStr, 0)) {
            int err = br_ssl_engine_last_error(&cc->eng);
            char msg[64];
            snprintf(msg, sizeof(msg), "Error: TLS reset failed (err=%d)\n", err);
            zenith::print(msg);
            free(respBuf);
            zenith::closesocket(fd);
            zenith::exit(1);
        }

        if (verbose) {
            zenith::print("TLS handshake...\n");
        }

        // Run TLS I/O loop
        respLen = tls_exchange(fd, &cc->eng, request, reqLen,
                               respBuf, RESP_MAX, verbose);

        if (verbose && respLen > 0) {
            zenith::print("TLS connection established\n");
        }

        // Cleanup (we don't bother freeing everything since we're exiting)
    } else {
        // ---- Plain HTTP ----
        respLen = plain_http_exchange(fd, request, reqLen, respBuf, RESP_MAX);

        if (respLen == -2) {
            zenith::print("\nAborted.\n");
            zenith::closesocket(fd);
            zenith::exit(0);
        }
    }

    zenith::closesocket(fd);

    if (respLen <= 0) {
        zenith::print("Error: no response received\n");
        zenith::exit(1);
    }

    respBuf[respLen] = '\0';
    print_response(respBuf, respLen, verbose);

    zenith::exit(0);
}
