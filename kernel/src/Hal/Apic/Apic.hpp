/*
    * Apic.hpp
    * Local APIC (Advanced Programmable Interrupt Controller)
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <cstdint>

namespace Hal {
    namespace LocalApic {
        // Local APIC register offsets
        constexpr uint32_t REG_ID               = 0x020;
        constexpr uint32_t REG_VERSION           = 0x030;
        constexpr uint32_t REG_TPR               = 0x080;
        constexpr uint32_t REG_EOI               = 0x0B0;
        constexpr uint32_t REG_SPURIOUS          = 0x0F0;
        constexpr uint32_t REG_ICR_LOW           = 0x300;
        constexpr uint32_t REG_ICR_HIGH          = 0x310;
        constexpr uint32_t REG_TIMER_LVT         = 0x320;
        constexpr uint32_t REG_LINT0_LVT         = 0x350;
        constexpr uint32_t REG_LINT1_LVT         = 0x360;
        constexpr uint32_t REG_ERROR_LVT         = 0x370;
        constexpr uint32_t REG_TIMER_INITIAL      = 0x380;
        constexpr uint32_t REG_TIMER_CURRENT      = 0x390;
        constexpr uint32_t REG_TIMER_DIVIDE       = 0x3E0;

        // Spurious vector number
        constexpr uint8_t SPURIOUS_VECTOR = 0xFF;

        // MSR for APIC base
        constexpr uint32_t MSR_APIC_BASE = 0x1B;

        void Initialize(uint64_t apicBasePhys);
        void SendEOI();
        uint32_t GetId();

        uint32_t ReadRegister(uint32_t reg);
        void WriteRegister(uint32_t reg, uint32_t value);
    };
};
