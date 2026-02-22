/*
    * Power.hpp
    * SYS_RESET, SYS_SHUTDOWN syscalls
    * Copyright (c) 2026 Daniel Hammer
*/

#pragma once
#include <cstdint>

namespace Zenith {

    static void Sys_Reset() {
        /*
            Triple fault for now; TODO: implement UEFI runtime function for clean reboot.

            We implement the triple fault by loading a null IDT into the IDT register,
            and then immediately triggering an interrupt.

            This technique should pretty much work across the board but it's of course
            better to use the UEFI runtime API as it has a method for this purpose,
            along with shutdown.
        */
       
        struct [[gnu::packed]] { uint16_t limit; uint64_t base; } nullIdt = {0, 0};
        asm volatile("lidt %0; int $0x03" :: "m"(nullIdt));
        __builtin_unreachable();
    }
};
