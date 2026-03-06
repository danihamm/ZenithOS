/*
    * FADT.cpp
    * Fixed ACPI Description Table parsing
    * Copyright (c) 2026 Daniel Hammer
*/

#include "FADT.hpp"
#include <Terminal/Terminal.hpp>
#include <CppLib/Stream.hpp>
#include <Memory/HHDM.hpp>

using namespace Kt;

namespace Hal {
    namespace FADT {

        static bool SignatureMatch(const char* sig, const char* target, int len) {
            for (int i = 0; i < len; i++) {
                if (sig[i] != target[i]) return false;
            }
            return true;
        }

        static Table* FindFADTInXSDT(ACPI::CommonSDTHeader* xsdt) {
            uint32_t entryCount = (xsdt->Length - sizeof(ACPI::CommonSDTHeader)) / 8;
            uint64_t* entries = (uint64_t*)((uint64_t)xsdt + sizeof(ACPI::CommonSDTHeader));

            for (uint32_t i = 0; i < entryCount; i++) {
                auto* header = (ACPI::CommonSDTHeader*)Memory::HHDM(entries[i]);
                if (SignatureMatch(header->Signature, "FACP", 4)) {
                    return (Table*)header;
                }
            }

            return nullptr;
        }

        bool Parse(ACPI::CommonSDTHeader* xsdt, ParsedFADT& result) {
            result.Valid = false;

            auto* fadt = FindFADTInXSDT(xsdt);
            if (fadt == nullptr) {
                KernelLogStream(ERROR, "FADT") << "FADT table not found in XSDT";
                return false;
            }

            if (!ACPI::TestChecksum(&fadt->Header)) {
                KernelLogStream(ERROR, "FADT") << "FADT checksum failed";
                return false;
            }

            KernelLogStream(OK, "FADT") << "Found FADT table";

            // Prefer X_Dsdt (64-bit) if available, otherwise fall back to 32-bit Dsdt
            if (fadt->Header.Length >= offsetof(Table, X_Dsdt) + 8 && fadt->X_Dsdt != 0) {
                result.DsdtAddress = fadt->X_Dsdt;
            } else {
                result.DsdtAddress = fadt->Dsdt;
            }

            result.PM1aControlBlock = fadt->PM1aControlBlock;
            result.PM1bControlBlock = fadt->PM1bControlBlock;
            result.SMI_CommandPort  = fadt->SMI_CommandPort;
            result.AcpiEnable       = fadt->AcpiEnable;
            result.AcpiDisable      = fadt->AcpiDisable;
            result.Valid            = true;

            KernelLogStream(INFO, "FADT") << "DSDT at " << base::hex << result.DsdtAddress;
            KernelLogStream(INFO, "FADT") << "PM1a_CNT_BLK: " << base::hex << (uint64_t)result.PM1aControlBlock;

            if (result.PM1bControlBlock != 0) {
                KernelLogStream(INFO, "FADT") << "PM1b_CNT_BLK: " << base::hex << (uint64_t)result.PM1bControlBlock;
            }

            return true;
        }
    };
};
