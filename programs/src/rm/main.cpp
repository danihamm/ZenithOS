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

    if (*args) {
        const char* arg = montauk::skip_spaces((const char *)&args);
        
        if (*arg != '\0') {
            int fd = montauk::open(arg);
            if (fd < 0) {
                montauk::print("file not found\n");
            } else {
                montauk::close(fd);
                montauk::fdelete(arg);
            }
        }
    } else {
        montauk::print("usage: rm [file]\n");
        montauk::exit(-1);
    }

    montauk::exit(0);
}