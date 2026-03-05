/*
 * helpers.cpp
 * Pixel drawing and string/number helpers
 * Copyright (c) 2026 Daniel Hammer
 */

#include "spreadsheet.h"

void px_fill(uint32_t* px, int bw, int bh,
             int x, int y, int w, int h, Color c) {
    uint32_t v = c.to_pixel();
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > bw ? bw : x + w;
    int y1 = y + h > bh ? bh : y + h;
    for (int row = y0; row < y1; row++)
        for (int col = x0; col < x1; col++)
            px[row * bw + col] = v;
}

void px_hline(uint32_t* px, int bw, int bh,
              int x, int y, int w, Color c) {
    if (y < 0 || y >= bh) return;
    uint32_t v = c.to_pixel();
    int x0 = x < 0 ? 0 : x;
    int x1 = x + w > bw ? bw : x + w;
    for (int col = x0; col < x1; col++)
        px[y * bw + col] = v;
}

void px_vline(uint32_t* px, int bw, int bh,
              int x, int y, int h, Color c) {
    if (x < 0 || x >= bw) return;
    uint32_t v = c.to_pixel();
    int y0 = y < 0 ? 0 : y;
    int y1 = y + h > bh ? bh : y + h;
    for (int row = y0; row < y1; row++)
        px[row * bw + x] = v;
}

void px_rect(uint32_t* px, int bw, int bh,
             int x, int y, int w, int h, Color c) {
    px_hline(px, bw, bh, x, y, w, c);
    px_hline(px, bw, bh, x, y + h - 1, w, c);
    px_vline(px, bw, bh, x, y, h, c);
    px_vline(px, bw, bh, x + w - 1, y, h, c);
}

void px_fill_rounded(uint32_t* px, int bw, int bh,
                     int x, int y, int w, int h, int r, Color c) {
    uint32_t v = c.to_pixel();
    for (int row = 0; row < h; row++) {
        int py = y + row;
        if (py < 0 || py >= bh) continue;
        int inset = 0;
        if (row < r) {
            int dy = r - 1 - row;
            if (r == 3) {
                if (dy >= 2) inset = 2;
                else if (dy >= 1) inset = 1;
            } else {
                for (int i = r; i > 0; i--) {
                    int dx = r - i;
                    if (dx * dx + dy * dy < r * r) { inset = i; break; }
                }
            }
        } else if (row >= h - r) {
            int dy = row - (h - r);
            if (r == 3) {
                if (dy >= 2) inset = 2;
                else if (dy >= 1) inset = 1;
            } else {
                for (int i = r; i > 0; i--) {
                    int dx = r - i;
                    if (dx * dx + dy * dy < r * r) { inset = i; break; }
                }
            }
        }
        int x0 = x + inset;
        int x1 = x + w - inset;
        if (x0 < 0) x0 = 0;
        if (x1 > bw) x1 = bw;
        for (int col = x0; col < x1; col++)
            px[py * bw + col] = v;
    }
}

int str_len(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

void str_cpy(char* dst, const char* src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_alpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
char to_upper(char c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

double str_to_double(const char* s, bool* ok) {
    *ok = false;
    if (!s || !s[0]) return 0;

    int i = 0;
    bool neg = false;
    if (s[i] == '-') { neg = true; i++; }
    else if (s[i] == '+') i++;

    double result = 0;
    bool has_digit = false;
    while (is_digit(s[i])) {
        result = result * 10 + (s[i] - '0');
        has_digit = true;
        i++;
    }

    if (s[i] == '.') {
        i++;
        double frac = 0.1;
        while (is_digit(s[i])) {
            result += (s[i] - '0') * frac;
            frac *= 0.1;
            has_digit = true;
            i++;
        }
    }

    if (!has_digit) return 0;
    while (s[i] == ' ') i++;
    if (s[i] != '\0') return 0;

    *ok = true;
    return neg ? -result : result;
}

void double_to_str(char* buf, int max, double v) {
    bool neg = false;
    if (v < 0) { neg = true; v = -v; }

    long long integer_part = (long long)v;
    double frac = v - (double)integer_part;

    if (frac < 0.005) {
        int i = 0;
        if (neg && integer_part != 0) buf[i++] = '-';
        if (integer_part == 0) {
            buf[i++] = '0';
        } else {
            char tmp[32];
            int t = 0;
            long long n = integer_part;
            while (n > 0 && t < 30) { tmp[t++] = '0' + (int)(n % 10); n /= 10; }
            while (t > 0 && i < max - 1) buf[i++] = tmp[--t];
        }
        buf[i] = '\0';
        return;
    }

    long long rounded = (long long)(v * 100 + 0.5);
    long long int_part = rounded / 100;
    int dec_part = (int)(rounded % 100);

    int i = 0;
    if (neg) buf[i++] = '-';
    if (int_part == 0) {
        buf[i++] = '0';
    } else {
        char tmp[32];
        int t = 0;
        long long n = int_part;
        while (n > 0 && t < 30) { tmp[t++] = '0' + (int)(n % 10); n /= 10; }
        while (t > 0 && i < max - 1) buf[i++] = tmp[--t];
    }
    if (i < max - 3) {
        buf[i++] = '.';
        buf[i++] = '0' + dec_part / 10;
        buf[i++] = '0' + dec_part % 10;
    }
    buf[i] = '\0';

    int len = str_len(buf);
    if (len > 0) {
        bool has_dot = false;
        for (int j = 0; j < len; j++) if (buf[j] == '.') has_dot = true;
        if (has_dot) {
            while (len > 1 && buf[len - 1] == '0') len--;
            if (len > 1 && buf[len - 1] == '.') len--;
            buf[len] = '\0';
        }
    }
}
