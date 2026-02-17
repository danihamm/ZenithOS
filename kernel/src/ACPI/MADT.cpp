/*
    * MADT.cpp
    * Multiple APIC Description Table parsing
    * Copyright (c) 2025 Daniel Hammer
*/

#include "MADT.hpp"
#include <Terminal/Terminal.hpp>
#include <CppLib/Stream.hpp>
#include <Memory/HHDM.hpp>
#include <Libraries/Memory.hpp>

using namespace Kt;

namespace Hal {
    namespace MADT {

        static bool SignatureMatch(const char* sig, const char* target, int len) {
            for (int i = 0; i < len; i++) {
                if (sig[i] != target[i]) return false;
            }
            return true;
        }

        static ACPI::CommonSDTHeader* FindMADTInXSDT(ACPI::CommonSDTHeader* xsdt) {
            uint32_t entryCount = (xsdt->Length - sizeof(ACPI::CommonSDTHeader)) / 8;
            uint64_t* entries = (uint64_t*)((uint64_t)xsdt + sizeof(ACPI::CommonSDTHeader));

            for (uint32_t i = 0; i < entryCount; i++) {
                auto* header = (ACPI::CommonSDTHeader*)Memory::HHDM(entries[i]);
                if (SignatureMatch(header->Signature, "APIC", 4)) {
                    return header;
                }
            }

            return nullptr;
        }

        bool Parse(ACPI::CommonSDTHeader* xsdt, ParsedMADT& result) {
            auto* madtHeader = FindMADTInXSDT(xsdt);
            if (madtHeader == nullptr) {
                KernelLogStream(ERROR, "MADT") << "MADT table not found in XSDT";
                return false;
            }

            if (!ACPI::TestChecksum(madtHeader)) {
                KernelLogStream(ERROR, "MADT") << "MADT checksum failed";
                return false;
            }

            KernelLogStream(OK, "MADT") << "Found MADT table";

            auto* madt = (Header*)madtHeader;
            result.LocalApicAddress = madt->LocalApicAddress;
            result.OverrideCount = 0;
            result.LocalApicCount = 0;
            result.IoApicAddress = 0;
            result.IoApicId = 0;
            result.IoApicGsiBase = 0;

            KernelLogStream(INFO, "MADT") << "Local APIC address: " << base::hex << (uint64_t)result.LocalApicAddress;

            uint32_t offset = sizeof(Header);
            uint32_t length = madt->SDTHeader.Length;

            while (offset < length) {
                auto* entry = (EntryHeader*)((uint64_t)madt + offset);

                switch (entry->Type) {
                    case 0: { // Processor Local APIC
                        auto* lapic = (LocalApicEntry*)entry;
                        if (result.LocalApicCount < ParsedMADT::MaxLocalApics) {
                            result.LocalApics[result.LocalApicCount] = *lapic;
                            result.LocalApicCount++;
                        }
                        KernelLogStream(DEBUG, "MADT") << "Local APIC: processor=" << (uint64_t)lapic->ProcessorId
                            << " id=" << (uint64_t)lapic->ApicId
                            << " flags=" << base::hex << (uint64_t)lapic->Flags;
                        break;
                    }

                    case 1: { // I/O APIC
                        auto* ioapic = (IoApicEntry*)entry;
                        result.IoApicAddress = ioapic->IoApicAddress;
                        result.IoApicId = ioapic->IoApicId;
                        result.IoApicGsiBase = ioapic->GlobalSystemInterruptBase;
                        KernelLogStream(DEBUG, "MADT") << "IOAPIC: id=" << (uint64_t)ioapic->IoApicId
                            << " address=" << base::hex << (uint64_t)ioapic->IoApicAddress
                            << " GSI base=" << base::dec << (uint64_t)ioapic->GlobalSystemInterruptBase;
                        break;
                    }

                    case 2: { // Interrupt Source Override
                        auto* iso = (InterruptSourceOverride*)entry;
                        if (result.OverrideCount < ParsedMADT::MaxOverrides) {
                            result.Overrides[result.OverrideCount] = *iso;
                            result.OverrideCount++;
                        }
                        KernelLogStream(DEBUG, "MADT") << "IRQ Override: bus=" << (uint64_t)iso->BusSource
                            << " irq=" << (uint64_t)iso->IrqSource
                            << " -> GSI " << (uint64_t)iso->GlobalSystemInterrupt
                            << " flags=" << base::hex << (uint64_t)iso->Flags;
                        break;
                    }

                    case 4: { // NMI
                        auto* nmi = (NmiEntry*)entry;
                        KernelLogStream(DEBUG, "MADT") << "NMI: processor=" << (uint64_t)nmi->ProcessorId
                            << " lint=" << (uint64_t)nmi->Lint;
                        break;
                    }

                    case 5: { // Local APIC Address Override
                        auto* override = (LocalApicAddressOverride*)entry;
                        result.LocalApicAddress = override->LocalApicAddress;
                        KernelLogStream(DEBUG, "MADT") << "Local APIC address override: " << base::hex << result.LocalApicAddress;
                        break;
                    }

                    default:
                        KernelLogStream(DEBUG, "MADT") << "Unknown MADT entry type: " << (uint64_t)entry->Type;
                        break;
                }

                offset += entry->Length;
            }

            KernelLogStream(OK, "MADT") << "Parsed " << base::dec
                << (uint64_t)result.LocalApicCount << " local APICs, "
                << (uint64_t)result.OverrideCount << " overrides";

            return true;
        }
    };
};
