/*
    * IoApic.hpp
    * I/O APIC (I/O Advanced Programmable Interrupt Controller)
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <cstdint>
#include <ACPI/MADT.hpp>

namespace Hal {
    namespace IoApic {
        // IOAPIC register indices (written to IOREGSEL)
        constexpr uint32_t IOAPICID  = 0x00;
        constexpr uint32_t IOAPICVER = 0x01;
        constexpr uint32_t IOAPICARB = 0x02;
        // Redirection table entries start at 0x10, each entry is 2 x 32-bit registers
        constexpr uint32_t IOREDTBL_BASE = 0x10;

        // Redirection entry flags
        constexpr uint64_t REDIR_MASKED          = (1ULL << 16);
        constexpr uint64_t REDIR_LEVEL_TRIGGER   = (1ULL << 15);
        constexpr uint64_t REDIR_ACTIVE_LOW      = (1ULL << 13);
        constexpr uint64_t REDIR_LOGICAL_DEST    = (1ULL << 11);

        // IRQ vector base: hardware IRQs start at vector 32
        constexpr uint8_t IRQ_VECTOR_BASE = 32;

        void Initialize(uint64_t ioApicBasePhys, uint32_t gsiBase,
                         MADT::InterruptSourceOverride* overrides, int overrideCount);

        void SetRedirectionEntry(uint8_t irq, uint64_t entry);
        uint64_t GetRedirectionEntry(uint8_t irq);

        void MaskIrq(uint8_t irq);
        void UnmaskIrq(uint8_t irq);

        // Route an ISA IRQ to a specific vector, applying source overrides
        void RouteIrq(uint8_t isaIrq, uint8_t vector, uint8_t destinationApicId);

        // Get the GSI for a given ISA IRQ (applies overrides)
        uint32_t GetGsiForIrq(uint8_t isaIrq);
    };
};
