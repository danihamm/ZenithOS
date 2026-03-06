/*
    * AmlParser.hpp
    * Primitive AML bytecode parser for extracting ACPI sleep state values
    * Copyright (c) 2026 Daniel Hammer
*/

#pragma once
#include <cstdint>

namespace Hal {
    namespace AML {

        // AML opcodes used during \_S5_ parsing
        static constexpr uint8_t NameOp      = 0x08;
        static constexpr uint8_t PackageOp   = 0x12;
        static constexpr uint8_t ZeroOp      = 0x00;
        static constexpr uint8_t OneOp       = 0x01;
        static constexpr uint8_t OnesOp      = 0xFF;
        static constexpr uint8_t BytePrefix  = 0x0A;
        static constexpr uint8_t WordPrefix  = 0x0B;
        static constexpr uint8_t DWordPrefix = 0x0C;

        struct S5Object {
            uint16_t SLP_TYPa;
            uint16_t SLP_TYPb;
            bool     Valid;
        };

        // Parse a DSDT (or SSDT) AML block to find the \_S5_ object.
        // dsdtData points to the CommonSDTHeader of the DSDT (HHDM-mapped).
        // Returns the parsed S5 values on success.
        S5Object FindS5(void* dsdtData);

    };
};
