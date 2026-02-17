/*
    * ApicInit.hpp
    * APIC subsystem initialization
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <ACPI/ACPI.hpp>

namespace Hal {
    // Initialize the full APIC subsystem:
    // 1. Parse MADT from XSDT
    // 2. Disable legacy PIC
    // 3. Install IRQ stubs in IDT
    // 4. Initialize Local APIC
    // 5. Initialize IOAPIC
    // 6. Route keyboard (IRQ1) and mouse (IRQ12)
    // 7. Enable interrupts
    //
    // xsdt: pointer to the XSDT (already HHDM-mapped)
    void ApicInitialize(ACPI::CommonSDTHeader* xsdt);
};
