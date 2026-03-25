/*
    * main.cpp
    * touch command (file create)
    * Copyright (c) 2026 Daniel Hammer
*/

#include <montauk/syscall.h>
#include <montauk/string.h>

extern "C" void _start() {
    char args[256];
    montauk::getargs((char *)&args, 256);

    const char* path = montauk::skip_spaces((const char *)&args);
    if (*path == '\0') {
        montauk::print("usage: touch [filename]\n");
        montauk::exit(-1);
    }

    int fd = montauk::open(path);
    if (fd >= 0) {
        montauk::close(fd);
        montauk::exit(0);
    }

    fd = montauk::fcreate(path);
    if (fd < 0) {
        montauk::print("touch: failed to create file\n");
        montauk::exit(-1);
    }

    montauk::close(fd);
    montauk::exit(0);
}
