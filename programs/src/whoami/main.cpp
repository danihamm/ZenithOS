/*
    * main.cpp
    * whoami - Print the current username
    * Copyright (c) 2026 Daniel Hammer
*/

#include <montauk/syscall.h>
#include <montauk/string.h>
#include <montauk/heap.h>

/*
    Mar 24, 2026 - update to use getuser() syscall
*/
extern "C" void _start() {
    char username[32] = { };
    montauk::getuser((char*)&username, 32);
    montauk::print((const char*)username);
    montauk::print("\n");

    montauk::exit(0);
}
