/*
    * Interrupts.cpp
    * Hardware interrupt registration and dispatch
    * Copyright (c) 2025 Daniel Hammer
*/

#include "Interrupts.hpp"
#include "Apic.hpp"
#include <Hal/IDT.hpp>
#include <Terminal/Terminal.hpp>
#include <CppLib/Stream.hpp>

// Assembly-defined stub table and spurious handler
extern "C" void* IrqStubTable[Hal::IRQ_COUNT];
extern "C" void IrqStubSpurious();

namespace Hal {
    // Dispatch table: one handler per IRQ
    static IrqHandler g_irqHandlers[IRQ_COUNT] = {};

    void RegisterIrqHandler(uint8_t irq, IrqHandler handler) {
        if (irq >= IRQ_COUNT) return;
        g_irqHandlers[irq] = handler;
        Kt::KernelLogStream(Kt::DEBUG, "IRQ") << "Registered handler for IRQ " << base::dec << (uint64_t)irq
            << " (vector " << (uint64_t)(IRQ_VECTOR_BASE + irq) << ")";
    }

    void InitializeIrqHandlers() {
        // Install IRQ stubs into IDT vectors 32..55
        for (int i = 0; i < IRQ_COUNT; i++) {
            IDTEncodeInterrupt(IRQ_VECTOR_BASE + i, IrqStubTable[i], 0x8E);
        }

        // Install spurious interrupt handler at vector 0xFF
        IDTEncodeInterrupt(0xFF, (void*)IrqStubSpurious, 0x8E);

        Kt::KernelLogStream(Kt::OK, "IRQ") << "Installed " << base::dec << (uint64_t)IRQ_COUNT
            << " IRQ stubs (vectors " << (uint64_t)IRQ_VECTOR_BASE << "-"
            << (uint64_t)(IRQ_VECTOR_BASE + IRQ_COUNT - 1) << ")";
    }
};

// C linkage dispatch function called from assembly stubs
extern "C" void HalIrqDispatch(uint64_t irqNumber) {
    if (irqNumber < Hal::IRQ_COUNT && Hal::g_irqHandlers[irqNumber] != nullptr) {
        Hal::g_irqHandlers[irqNumber]((uint8_t)irqNumber);
    }

    // Send End of Interrupt to the Local APIC
    Hal::LocalApic::SendEOI();
}
