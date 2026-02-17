/*
    * main.cpp
    * Hello world program for ZenithOS
    * Copyright (c) 2025 Daniel Hammer
*/

#include <zenith/syscall.h>

extern "C" void _start() {


    zenith::print("Hello from userspace!\n");

    // while(true) {
    //     zenith::print("ab");
    // }

}
