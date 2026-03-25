/*
    * main.cpp
    * rm - command to remove files
    * Copyright (c) 2026 Daniel Hammer
*/

#include <montauk/syscall.h>
#include <montauk/string.h>

extern "C" void _start() {
    char args[256];
    montauk::getargs((char *)&args, 256);

    const char* path = montauk::skip_spaces((const char *)&args);
    if (*path == '\0') {
        montauk::print("usage: rm [file]\n");
        montauk::exit(-1);
    }

    if (montauk::fdelete(path) < 0) {
        montauk::print("rm: failed to remove file\n");
        montauk::exit(-1);
    }

    montauk::exit(0);
}
