/*
    * MSR.hpp
    * Model-Specific Register read/write helpers
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <cstdint>

namespace Hal {

    inline uint64_t ReadMSR(uint32_t msr) {
        uint32_t lo, hi;
        asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
        return ((uint64_t)hi << 32) | lo;
    }

    inline void WriteMSR(uint32_t msr, uint64_t value) {
        uint32_t lo = (uint32_t)(value & 0xFFFFFFFF);
        uint32_t hi = (uint32_t)(value >> 32);
        asm volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
    }

    // Well-known MSR addresses
    static constexpr uint32_t IA32_EFER  = 0xC0000080;
    static constexpr uint32_t IA32_STAR  = 0xC0000081;
    static constexpr uint32_t IA32_LSTAR = 0xC0000082;
    static constexpr uint32_t IA32_FMASK = 0xC0000084;
    static constexpr uint32_t IA32_PAT   = 0x00000277;

    // PAT memory type encodings
    static constexpr uint8_t PAT_UC  = 0x00; // Uncacheable
    static constexpr uint8_t PAT_WC  = 0x01; // Write Combining
    static constexpr uint8_t PAT_WT  = 0x04; // Write Through
    static constexpr uint8_t PAT_WP  = 0x05; // Write Protect
    static constexpr uint8_t PAT_WB  = 0x06; // Write Back
    static constexpr uint8_t PAT_UCM = 0x07; // UC- (UC minus)

    // Program PAT so entry 1 = WC (default is WT).
    // PAT index is selected by PTE bits: PAT(bit7) | PCD(bit4) | PWT(bit3)
    //   Entry 0 (000) = WB   — normal memory (unchanged)
    //   Entry 1 (001) = WC   — framebuffers  (was WT)
    //   Entry 2 (010) = UC-  — (unchanged)
    //   Entry 3 (011) = UC   — MMIO registers (unchanged)
    //   Entries 4-7: unchanged from defaults
    inline void InitializePAT() {
        uint64_t pat =
            ((uint64_t)PAT_WB  <<  0) | // Entry 0: WB
            ((uint64_t)PAT_WC  <<  8) | // Entry 1: WC  (was WT)
            ((uint64_t)PAT_UCM << 16) | // Entry 2: UC-
            ((uint64_t)PAT_UC  << 24) | // Entry 3: UC
            ((uint64_t)PAT_WB  << 32) | // Entry 4: WB
            ((uint64_t)PAT_WT  << 40) | // Entry 5: WT
            ((uint64_t)PAT_UCM << 48) | // Entry 6: UC-
            ((uint64_t)PAT_UC  << 56);  // Entry 7: UC
        WriteMSR(IA32_PAT, pat);
    }

}
