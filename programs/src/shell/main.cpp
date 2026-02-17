/*
    * main.cpp
    * Interactive shell for ZenithOS
    * Copyright (c) 2025 Daniel Hammer
*/

#include <zenith/syscall.h>

static bool streq(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) return false;
        a++; b++;
    }
    return *a == *b;
}

static bool starts_with(const char* str, const char* prefix) {
    while (*prefix) {
        if (*str != *prefix) return false;
        str++; prefix++;
    }
    return true;
}

static const char* skip_spaces(const char* s) {
    while (*s == ' ') s++;
    return s;
}

static void print_int(uint64_t n) {
    if (n == 0) {
        zenith::putchar('0');
        return;
    }
    char buf[20];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    for (int j = i - 1; j >= 0; j--) {
        zenith::putchar(buf[j]);
    }
}

static void prompt() {
    zenith::print("zenith> ");
}

static void cmd_help() {
    zenith::print("Available commands:\n");
    zenith::print("  help          Show this help message\n");
    zenith::print("  info          Show system information\n");
    zenith::print("  ls            List ramdisk files\n");
    zenith::print("  cat <file>    Display file contents\n");
    zenith::print("  ping <ip>     Send ICMP echo requests\n");
    zenith::print("  uptime        Show uptime in milliseconds\n");
    zenith::print("  clear         Clear the screen\n");
    zenith::print("  exit          Exit the shell\n");
}

static void cmd_info() {
    Zenith::SysInfo info;
    zenith::get_info(&info);
    zenith::print(info.osName);
    zenith::print(" v");
    zenith::print(info.osVersion);
    zenith::print("\n");
    zenith::print("Syscall API version: ");
    print_int(info.apiVersion);
    zenith::putchar('\n');
}

static void cmd_ls() {
    const char* entries[64];
    int count = zenith::readdir("0:/", entries, 64);
    if (count <= 0) {
        zenith::print("(empty)\n");
        return;
    }
    for (int i = 0; i < count; i++) {
        zenith::print("  ");
        zenith::print(entries[i]);
        zenith::putchar('\n');
    }
}

static void cmd_cat(const char* arg) {
    arg = skip_spaces(arg);
    if (*arg == '\0') {
        zenith::print("Usage: cat <filename>\n");
        return;
    }

    // Build path "0:/<filename>"
    char path[128];
    const char* prefix = "0:/";
    int i = 0;
    while (prefix[i]) { path[i] = prefix[i]; i++; }
    int j = 0;
    while (arg[j] && i < 126) { path[i++] = arg[j++]; }
    path[i] = '\0';

    int handle = zenith::open(path);
    if (handle < 0) {
        zenith::print("Error: cannot open '");
        zenith::print(arg);
        zenith::print("'\n");
        return;
    }

    uint64_t size = zenith::getsize(handle);
    if (size == 0) {
        zenith::close(handle);
        return;
    }

    // Read in chunks
    uint8_t buf[512];
    uint64_t offset = 0;
    while (offset < size) {
        uint64_t chunk = size - offset;
        if (chunk > sizeof(buf) - 1) chunk = sizeof(buf) - 1;
        int bytesRead = zenith::read(handle, buf, offset, chunk);
        if (bytesRead <= 0) break;
        buf[bytesRead] = '\0';
        zenith::print((const char*)buf);
        offset += bytesRead;
    }

    zenith::close(handle);
    zenith::putchar('\n');
}

static void cmd_uptime() {
    uint64_t ms = zenith::get_milliseconds();
    uint64_t secs = ms / 1000;
    uint64_t mins = secs / 60;
    secs %= 60;
    ms %= 1000;

    zenith::print("Uptime: ");
    print_int(mins);
    zenith::print("m ");
    print_int(secs);
    zenith::print("s ");
    print_int(ms);
    zenith::print("ms\n");
}

static bool parse_ip(const char* s, uint32_t* out) {
    // Parse "a.b.c.d" into a uint32_t in network byte order (little-endian stored)
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
            val = 0;
            hasDigit = false;
            if (c == '\0') break;
        } else {
            return false;
        }
    }

    if (idx != 4) return false;
    *out = octets[0] | (octets[1] << 8) | (octets[2] << 16) | (octets[3] << 24);
    return true;
}

static void print_ip(uint32_t ip) {
    print_int(ip & 0xFF);
    zenith::putchar('.');
    print_int((ip >> 8) & 0xFF);
    zenith::putchar('.');
    print_int((ip >> 16) & 0xFF);
    zenith::putchar('.');
    print_int((ip >> 24) & 0xFF);
}

static void cmd_ping(const char* arg) {
    arg = skip_spaces(arg);
    if (*arg == '\0') {
        zenith::print("Usage: ping <ip address>\n");
        return;
    }

    uint32_t ip;
    if (!parse_ip(arg, &ip)) {
        zenith::print("Invalid IP address: ");
        zenith::print(arg);
        zenith::putchar('\n');
        return;
    }

    zenith::print("PING ");
    print_ip(ip);
    zenith::putchar('\n');

    for (int i = 0; i < 4; i++) {
        int32_t rtt = zenith::ping(ip, 3000);
        if (rtt < 0) {
            zenith::print("  Request timed out\n");
        } else {
            zenith::print("  Reply from ");
            print_ip(ip);
            zenith::print(": time=");
            print_int((uint64_t)rtt);
            zenith::print("ms\n");
        }
        if (i < 3) {
            zenith::sleep_ms(1000);
        }
    }
}

static void cmd_clear() {
    // Print enough newlines to scroll past visible content
    for (int i = 0; i < 50; i++) {
        zenith::putchar('\n');
    }
}

static void process_command(const char* line) {
    // Skip leading spaces
    line = skip_spaces(line);
    if (*line == '\0') return;

    if (streq(line, "help")) {
        cmd_help();
    } else if (streq(line, "info")) {
        cmd_info();
    } else if (streq(line, "ls")) {
        cmd_ls();
    } else if (starts_with(line, "cat ")) {
        cmd_cat(line + 4);
    } else if (streq(line, "cat")) {
        cmd_cat("");
    } else if (starts_with(line, "ping ")) {
        cmd_ping(line + 5);
    } else if (streq(line, "ping")) {
        cmd_ping("");
    } else if (streq(line, "uptime")) {
        cmd_uptime();
    } else if (streq(line, "clear")) {
        cmd_clear();
    } else if (streq(line, "exit")) {
        zenith::print("Goodbye.\n");
        zenith::exit(0);
    } else {
        zenith::print("Unknown command: ");
        zenith::print(line);
        zenith::print("\nType 'help' for available commands.\n");
    }
}

extern "C" void _start() {
    zenith::print("\n");
    zenith::print("  ZenithOS Shell v0.1\n");
    zenith::print("  Type 'help' for available commands.\n");
    zenith::print("\n");

    char line[256];
    int pos = 0;

    prompt();

    while (true) {
        char c = zenith::getchar();

        if (c == '\n') {
            zenith::putchar('\n');
            line[pos] = '\0';
            process_command(line);
            pos = 0;
            prompt();
        } else if (c == '\b') {
            if (pos > 0) {
                pos--;
                zenith::putchar('\b');
                zenith::putchar(' ');
                zenith::putchar('\b');
            }
        } else if (c >= ' ' && pos < 255) {
            line[pos++] = c;
            zenith::putchar(c);
        }
    }
}
