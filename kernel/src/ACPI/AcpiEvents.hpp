/*
    * AcpiEvents.hpp
    * ACPI fixed event handling (SCI interrupt, power button, etc.)
    * Copyright (c) 2026 Daniel Hammer
*/

#pragma once
#include <ACPI/ACPI.hpp>
#include <ACPI/FADT.hpp>

namespace Hal {
    namespace AcpiEvents {

        // Initialize ACPI event handling: route the SCI interrupt,
        // register the handler, and enable fixed events (power button).
        // Must be called after APIC and FADT initialization.
        void Initialize(const FADT::ParsedFADT& fadt);

        // Re-enable ACPI events after S3 resume.
        void Reinitialize();
    };
};
