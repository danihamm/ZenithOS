/*
    * IoApic.cpp
    * I/O APIC (I/O Advanced Programmable Interrupt Controller)
    * Copyright (c) 2025 Daniel Hammer
*/

#include "IoApic.hpp"
#include <Terminal/Terminal.hpp>
#include <CppLib/Stream.hpp>
#include <Memory/HHDM.hpp>

using namespace Kt;

namespace Hal {
    namespace IoApic {
        static volatile uint32_t* g_ioApicBase = nullptr;
        static uint32_t g_gsiBase = 0;
        static uint32_t g_maxRedirEntries = 0;

        // Stored overrides for IRQ remapping
        static MADT::InterruptSourceOverride g_overrides[MADT::ParsedMADT::MaxOverrides];
        static int g_overrideCount = 0;

        static uint32_t ReadRegister(uint32_t reg) {
            // Write the register index to IOREGSEL (offset 0x00)
            g_ioApicBase[0] = reg;
            // Read the value from IOWIN (offset 0x10 = index 4 in uint32_t array)
            return g_ioApicBase[4];
        }

        static void WriteRegister(uint32_t reg, uint32_t value) {
            g_ioApicBase[0] = reg;
            g_ioApicBase[4] = value;
        }

        void SetRedirectionEntry(uint8_t index, uint64_t entry) {
            uint32_t regLow  = IOREDTBL_BASE + (index * 2);
            uint32_t regHigh = IOREDTBL_BASE + (index * 2) + 1;

            WriteRegister(regHigh, (uint32_t)(entry >> 32));
            WriteRegister(regLow,  (uint32_t)(entry & 0xFFFFFFFF));
        }

        uint64_t GetRedirectionEntry(uint8_t index) {
            uint32_t regLow  = IOREDTBL_BASE + (index * 2);
            uint32_t regHigh = IOREDTBL_BASE + (index * 2) + 1;

            uint64_t low  = ReadRegister(regLow);
            uint64_t high = ReadRegister(regHigh);

            return (high << 32) | low;
        }

        void MaskIrq(uint8_t irq) {
            uint64_t entry = GetRedirectionEntry(irq);
            entry |= REDIR_MASKED;
            SetRedirectionEntry(irq, entry);
        }

        void UnmaskIrq(uint8_t irq) {
            uint64_t entry = GetRedirectionEntry(irq);
            entry &= ~REDIR_MASKED;
            SetRedirectionEntry(irq, entry);
        }

        uint32_t GetGsiForIrq(uint8_t isaIrq) {
            for (int i = 0; i < g_overrideCount; i++) {
                if (g_overrides[i].IrqSource == isaIrq) {
                    return g_overrides[i].GlobalSystemInterrupt;
                }
            }
            // No override found, identity map
            return isaIrq;
        }

        void RouteIrq(uint8_t isaIrq, uint8_t vector, uint8_t destinationApicId) {
            uint32_t gsi = GetGsiForIrq(isaIrq);
            uint32_t ioApicIndex = gsi - g_gsiBase;

            // Build redirection entry
            uint64_t entry = 0;
            entry |= vector;                                      // Vector
            entry |= ((uint64_t)destinationApicId << 56);          // Destination

            // Check if there's an override with specific polarity/trigger settings
            for (int i = 0; i < g_overrideCount; i++) {
                if (g_overrides[i].IrqSource == isaIrq) {
                    uint16_t flags = g_overrides[i].Flags;
                    // Polarity: bits 0-1 (0b11 = active low)
                    if ((flags & 0x03) == 0x03) {
                        entry |= REDIR_ACTIVE_LOW;
                    }
                    // Trigger mode: bits 2-3 (0b11 = level triggered)
                    if ((flags & 0x0C) == 0x0C) {
                        entry |= REDIR_LEVEL_TRIGGER;
                    }
                    break;
                }
            }

            SetRedirectionEntry(ioApicIndex, entry);

            KernelLogStream(DEBUG, "IOAPIC") << "Routed ISA IRQ " << base::dec << (uint64_t)isaIrq
                << " -> GSI " << (uint64_t)gsi
                << " -> vector " << (uint64_t)vector
                << " -> APIC " << (uint64_t)destinationApicId;
        }

        void Initialize(uint64_t ioApicBasePhys, uint32_t gsiBase,
                         MADT::InterruptSourceOverride* overrides, int overrideCount) {
            g_ioApicBase = (volatile uint32_t*)Memory::HHDM(ioApicBasePhys);
            g_gsiBase = gsiBase;

            // Store overrides
            g_overrideCount = overrideCount;
            for (int i = 0; i < overrideCount; i++) {
                g_overrides[i] = overrides[i];
            }

            // Read IOAPIC version and max redirection entries
            uint32_t version = ReadRegister(IOAPICVER);
            g_maxRedirEntries = ((version >> 16) & 0xFF) + 1;
            uint32_t ioapicId = (ReadRegister(IOAPICID) >> 24) & 0x0F;

            KernelLogStream(OK, "IOAPIC") << "IOAPIC initialized: id=" << base::dec << (uint64_t)ioapicId
                << " version=" << base::hex << (uint64_t)(version & 0xFF)
                << " entries=" << base::dec << (uint64_t)g_maxRedirEntries;

            // Mask all redirection entries initially
            for (uint32_t i = 0; i < g_maxRedirEntries; i++) {
                SetRedirectionEntry(i, REDIR_MASKED | (IRQ_VECTOR_BASE + i));
            }

            KernelLogStream(OK, "IOAPIC") << "All redirection entries masked";
        }
    };
};
