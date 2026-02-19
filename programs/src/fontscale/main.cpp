/*
    * main.cpp
    * fontscale - Change terminal font scale
    * Copyright (c) 2025-2026 Daniel Hammer
*/

#include <zenith/syscall.h>
#include <zenith/string.h>

static int atoi(const char* s) {
    int n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    return n;
}

static void print_int(int n) {
    char buf[16];
    int i = 0;
    if (n == 0) {
        zenith::putchar('0');
        return;
    }
    while (n > 0 && i < 15) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0) zenith::putchar(buf[--i]);
}

extern "C" void _start() {
    char args[128];
    int len = zenith::getargs(args, sizeof(args));

    if (len <= 0 || args[0] == '\0') {
        // No args: show current scale
        int sx, sy;
        zenith::get_termscale(&sx, &sy);
        int cols, rows;
        zenith::termsize(&cols, &rows);

        zenith::print("Font scale: ");
        print_int(sx);
        zenith::print("x");
        print_int(sy);
        zenith::print("  Terminal: ");
        print_int(cols);
        zenith::print("x");
        print_int(rows);
        zenith::putchar('\n');
        zenith::exit(0);
    }

    // Parse arguments
    const char* p = zenith::skip_spaces(args);
    int scale_x = atoi(p);

    // Skip past first number to find optional second
    while (*p >= '0' && *p <= '9') p++;
    p = zenith::skip_spaces(p);
    int scale_y = (*p >= '1' && *p <= '8') ? atoi(p) : scale_x;

    if (scale_x < 1 || scale_x > 8 || scale_y < 1 || scale_y > 8) {
        zenith::print("fontscale: scale must be 1-8\n");
        zenith::exit(1);
    }

    zenith::termscale(scale_x, scale_y);

    // Clear and show result
    zenith::print("\033[2J\033[H");

    int cols, rows;
    zenith::termsize(&cols, &rows);
    zenith::print("Font scale set to ");
    print_int(scale_x);
    zenith::print("x");
    print_int(scale_y);
    zenith::print("  (");
    print_int(cols);
    zenith::print("x");
    print_int(rows);
    zenith::print(")\n");

    zenith::exit(0);
}
