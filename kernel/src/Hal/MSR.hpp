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

}
