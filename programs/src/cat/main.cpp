/*
    * main.cpp
    * cat - Display file contents
    * Copyright (c) 2025-2026 Daniel Hammer
*/

#include <montauk/syscall.h>
#include <montauk/string.h>

extern "C" void _start() {
    char args[256];
    int len = montauk::getargs(args, sizeof(args));
    const char* path = montauk::skip_spaces(args);

    if (len <= 0 || *path == '\0') {
        montauk::print("Usage: cat <filename>\n");
        montauk::exit(1);
    }

    int handle = montauk::open(path);
    if (handle < 0) {
        montauk::print("cat: cannot open '");
        montauk::print(path);
        montauk::print("'\n");
        montauk::exit(1);
    }

    uint64_t size = montauk::getsize(handle);
    if (size == 0) {
        montauk::close(handle);
        montauk::exit(0);
    }

    uint8_t buf[512];
    uint64_t offset = 0;
    while (offset < size) {
        uint64_t chunk = size - offset;
        if (chunk > sizeof(buf) - 1) chunk = sizeof(buf) - 1;
        int bytesRead = montauk::read(handle, buf, offset, chunk);
        if (bytesRead <= 0) break;
        buf[bytesRead] = '\0';
        montauk::print((const char*)buf);
        offset += bytesRead;
    }

    montauk::close(handle);
    montauk::putchar('\n');
    montauk::exit(0);
}
