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

    if (*args) {
        const char* arg = montauk::skip_spaces((const char *)&args);
        int fd = montauk::open(arg);
        montauk::close(fd);

        if (fd >= 0) { /* file already exists */
            montauk::exit(0);
        } else {
            int fd = montauk::fcreate(arg);
            montauk::close(fd);

            montauk::exit(0);
        }
    } else {
        montauk::print("usage: touch [filename]\n");
        montauk::exit(-1);
    }
}