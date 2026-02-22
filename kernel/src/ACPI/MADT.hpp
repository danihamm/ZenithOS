/*
    * MADT.hpp
    * Multiple APIC Description Table parsing
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <cstdint>
#include <ACPI/ACPI.hpp>

namespace Hal {
    namespace MADT {
        struct Header {
            Hal::ACPI::CommonSDTHeader SDTHeader;
            uint32_t LocalApicAddress;
            uint32_t Flags;
        }__attribute__((packed));

        struct EntryHeader {
            uint8_t Type;
            uint8_t Length;
        }__attribute__((packed));

        // Type 0: Processor Local APIC
        struct LocalApicEntry {
            EntryHeader Header;
            uint8_t ProcessorId;
            uint8_t ApicId;
            uint32_t Flags;
        }__attribute__((packed));

        // Type 1: I/O APIC
        struct IoApicEntry {
            EntryHeader Header;
            uint8_t IoApicId;
            uint8_t Reserved;
            uint32_t IoApicAddress;
            uint32_t GlobalSystemInterruptBase;
        }__attribute__((packed));

        // Type 2: Interrupt Source Override
        struct InterruptSourceOverride {
            EntryHeader Header;
            uint8_t BusSource;
            uint8_t IrqSource;
            uint32_t GlobalSystemInterrupt;
            uint16_t Flags;
        }__attribute__((packed));

        // Type 4: Non-Maskable Interrupt
        struct NmiEntry {
            EntryHeader Header;
            uint8_t ProcessorId;
            uint16_t Flags;
            uint8_t Lint;
        }__attribute__((packed));

        // Type 5: Local APIC Address Override
        struct LocalApicAddressOverride {
            EntryHeader Header;
            uint16_t Reserved;
            uint64_t LocalApicAddress;
        }__attribute__((packed));

        struct ParsedMADT {
            uint64_t LocalApicAddress;
            uint64_t IoApicAddress;
            uint8_t IoApicId;
            uint32_t IoApicGsiBase;

            static constexpr int MaxOverrides = 16;
            InterruptSourceOverride Overrides[MaxOverrides];
            int OverrideCount;

            static constexpr int MaxLocalApics = 64;
            LocalApicEntry LocalApics[MaxLocalApics];
            int LocalApicCount;
        };

        bool Parse(ACPI::CommonSDTHeader* xsdt, ParsedMADT& result);
    };
};
