/*
    * AmlParser.hpp
    * AML bytecode parser — S5 extraction and interpreter initialization
    * Copyright (c) 2026 Daniel Hammer
*/

#pragma once
#include <cstdint>

namespace Hal {
    namespace AML {

        struct SleepObject {
            uint16_t SLP_TYPa;
            uint16_t SLP_TYPb;
            bool     Valid;
        };

        // Legacy compat alias
        using S5Object = SleepObject;

        // Parse a DSDT (or SSDT) AML block to find the \_S5_ object.
        S5Object FindS5(void* dsdtData);

        // Parse a DSDT to find any \_Sx_ object (x = 0-5) via brute-force scan.
        // Works on any DSDT regardless of complexity — does not require the
        // interpreter or namespace.
        SleepObject FindSleepState(void* dsdtData, int state);

        // Initialize the AML interpreter with the DSDT.
        // This loads the full table into the namespace and enables
        // method evaluation, device enumeration, and field access.
        // Should be called during boot after ACPI table discovery.
        void InitializeInterpreter(void* dsdtData);

    };
};
