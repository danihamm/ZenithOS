/*
    * AcpiShutdown.hpp
    * ACPI S5 (soft-off) shutdown via PM1 control registers
    * Copyright (c) 2026 Daniel Hammer
*/

#pragma once
#include <ACPI/ACPI.hpp>

namespace Hal {
    namespace AcpiShutdown {

        // Parse the FADT and DSDT from the XSDT to prepare shutdown values.
        // Must be called during boot before any shutdown attempt.
        void Initialize(ACPI::CommonSDTHeader* xsdt);

        // Returns true if ACPI shutdown is available (Initialize succeeded).
        bool IsAvailable();

        // Perform an ACPI S5 shutdown by writing to PM1a/PM1b control registers.
        // Does not return on success.
        void Shutdown();
    };
};
