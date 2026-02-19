/*
    * main.cpp
    * DNS lookup utility for ZenithOS
    * Usage: nslookup <hostname>
    * Copyright (c) 2025-2026 Daniel Hammer
*/

#include <zenith/syscall.h>
#include <zenith/string.h>

using zenith::skip_spaces;

static void print_ip(uint32_t ip) {
    auto print_int = [](uint32_t n) {
        if (n == 0) { zenith::putchar('0'); return; }
        char buf[4];
        int i = 0;
        while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
        for (int j = i - 1; j >= 0; j--) zenith::putchar(buf[j]);
    };

    print_int(ip & 0xFF);
    zenith::putchar('.');
    print_int((ip >> 8) & 0xFF);
    zenith::putchar('.');
    print_int((ip >> 16) & 0xFF);
    zenith::putchar('.');
    print_int((ip >> 24) & 0xFF);
}

extern "C" void _start() {
    char args[256];
    int len = zenith::getargs(args, sizeof(args));

    const char* hostname = skip_spaces(args);
    if (len <= 0 || hostname[0] == '\0') {
        zenith::print("Usage: nslookup <hostname>\n");
        zenith::print("Example: nslookup example.com\n");
        zenith::exit(0);
    }

    // Show DNS server
    Zenith::NetCfg cfg;
    zenith::get_netcfg(&cfg);

    zenith::print("Server:  ");
    print_ip(cfg.dnsServer);
    zenith::putchar('\n');

    zenith::print("Querying ");
    zenith::print(hostname);
    zenith::print("...\n");

    uint64_t start = zenith::get_milliseconds();
    uint32_t ip = zenith::resolve(hostname);
    uint64_t elapsed = zenith::get_milliseconds() - start;

    if (ip == 0) {
        zenith::print("Error: could not resolve ");
        zenith::print(hostname);
        zenith::putchar('\n');
        zenith::exit(1);
    }

    zenith::print("Name:    ");
    zenith::print(hostname);
    zenith::putchar('\n');
    zenith::print("Address: ");
    print_ip(ip);
    zenith::putchar('\n');

    // Print timing
    zenith::print("Time:    ");
    char buf[20];
    int i = 0;
    uint64_t ms = elapsed;
    if (ms == 0) { buf[i++] = '0'; }
    else { while (ms > 0) { buf[i++] = '0' + (ms % 10); ms /= 10; } }
    for (int j = i - 1; j >= 0; j--) zenith::putchar(buf[j]);
    zenith::print(" ms\n");

    zenith::exit(0);
}
