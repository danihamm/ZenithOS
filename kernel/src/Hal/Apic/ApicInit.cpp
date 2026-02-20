/*
    * ApicInit.cpp
    * APIC subsystem initialization
    * Copyright (c) 2025 Daniel Hammer
*/

#include "ApicInit.hpp"
#include "Pic.hpp"
#include "Apic.hpp"
#include "IoApic.hpp"
#include "Interrupts.hpp"
#include <ACPI/MADT.hpp>
#include <Hal/IDT.hpp>
#include <Terminal/Terminal.hpp>
#include <CppLib/Stream.hpp>
#include <Memory/Paging.hpp>
#include <Memory/HHDM.hpp>

using namespace Kt;

namespace Hal {
    static int g_detectedCpuCount = 0;

    int GetDetectedCpuCount() { return g_detectedCpuCount; }

    void ApicInitialize(ACPI::CommonSDTHeader* xsdt) {
        KernelLogStream(INFO, "APIC") << "Initializing APIC subsystem";

        // Step 1: Parse MADT
        MADT::ParsedMADT madt{};
        if (!MADT::Parse(xsdt, madt)) {
            KernelLogStream(ERROR, "APIC") << "Failed to parse MADT, cannot initialize APIC";
            return;
        }

        g_detectedCpuCount = madt.LocalApicCount;

        if (madt.IoApicAddress == 0) {
            KernelLogStream(ERROR, "APIC") << "No IOAPIC found in MADT";
            return;
        }

        // Step 2: Map APIC MMIO regions into kernel page tables
        // The HHDM only covers physical RAM; MMIO regions need explicit mapping.
        if (Memory::VMM::g_paging) {
            Memory::VMM::g_paging->MapMMIO(madt.LocalApicAddress, Memory::HHDM(madt.LocalApicAddress));
            KernelLogStream(DEBUG, "APIC") << "Mapped Local APIC MMIO at phys " << base::hex << madt.LocalApicAddress;

            Memory::VMM::g_paging->MapMMIO(madt.IoApicAddress, Memory::HHDM(madt.IoApicAddress));
            KernelLogStream(DEBUG, "APIC") << "Mapped IOAPIC MMIO at phys " << base::hex << madt.IoApicAddress;
        }

        // Step 3: Disable legacy 8259 PIC
        DisableLegacyPic();

        // Step 4: Install IRQ stubs into IDT
        InitializeIrqHandlers();
        IDTReload();

        // Step 5: Initialize Local APIC
        LocalApic::Initialize(madt.LocalApicAddress);

        // Step 6: Initialize IOAPIC
        IoApic::Initialize(madt.IoApicAddress, madt.IoApicGsiBase,
                            madt.Overrides, madt.OverrideCount);

        // Step 7: Route keyboard (IRQ1) and mouse (IRQ12) to BSP
        uint8_t bspApicId = (uint8_t)LocalApic::GetId();

        IoApic::RouteIrq(IRQ_KEYBOARD, IRQ_VECTOR_BASE + IRQ_KEYBOARD, bspApicId);
        IoApic::RouteIrq(IRQ_MOUSE,    IRQ_VECTOR_BASE + IRQ_MOUSE,    bspApicId);

        // Step 8: Enable interrupts
        asm volatile("sti");

        KernelLogStream(OK, "APIC") << "APIC subsystem initialized, interrupts enabled";
    }
};
