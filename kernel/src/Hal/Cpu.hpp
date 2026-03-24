/*
    * Cpu.hpp
    * CPU feature enablement helpers
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <cstdint>

namespace Hal {

    // Enable SSE/SSE2 — required for userspace programs compiled with SSE.
    // CR0: clear EM (bit 2), set MP (bit 1)
    // CR4: set OSFXSR (bit 9) and OSXMMEXCPT (bit 10)
    // Check if MONITOR/MWAIT is supported (CPUID.01H:ECX bit 3)
    inline bool HasMwait() {
        uint32_t ecx;
        asm volatile("cpuid" : "=c"(ecx) : "a"(1) : "ebx", "edx");
        return (ecx & (1 << 3)) != 0;
    }

    // Idle using MWAIT if available, otherwise HLT.
    // MWAIT can enter deeper C-states (C1E/C3/C6) for better
    // power and thermal efficiency than HLT (C1 only).
    // The monitored address is arbitrary -- we just need MONITOR
    // to arm the wake trigger; any interrupt wakes MWAIT.
    inline void IdleWait(volatile uint64_t* monitorAddr) {
        // MONITOR: set up the address monitoring range
        asm volatile("monitor" :: "a"(monitorAddr), "c"(0), "d"(0));
        // MWAIT: hint=0x00 (C1 state, platform-dependent deeper states)
        asm volatile("mwait" :: "a"(0x00), "c"(0));
    }

    inline void EnableSSE() {
        uint64_t cr0;
        asm volatile("mov %%cr0, %0" : "=r"(cr0));
        cr0 &= ~(1ULL << 2);  // Clear EM
        cr0 |=  (1ULL << 1);  // Set MP
        asm volatile("mov %0, %%cr0" :: "r"(cr0));

        uint64_t cr4;
        asm volatile("mov %%cr4, %0" : "=r"(cr4));
        cr4 |= (1ULL << 9);   // OSFXSR
        cr4 |= (1ULL << 10);  // OSXMMEXCPT
        asm volatile("mov %0, %%cr4" :: "r"(cr4));
    }

}
