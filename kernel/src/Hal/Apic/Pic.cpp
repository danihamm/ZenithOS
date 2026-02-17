/*
    * Pic.cpp
    * Legacy 8259 PIC disable
    * Copyright (c) 2025 Daniel Hammer
*/

#include "Pic.hpp"
#include <Io/IoPort.hpp>
#include <Terminal/Terminal.hpp>
#include <CppLib/Stream.hpp>

namespace Hal {
    // 8259 PIC ports
    constexpr uint16_t PIC1_COMMAND = 0x20;
    constexpr uint16_t PIC1_DATA    = 0x21;
    constexpr uint16_t PIC2_COMMAND = 0xA0;
    constexpr uint16_t PIC2_DATA    = 0xA1;

    // ICW1 flags
    constexpr uint8_t ICW1_INIT = 0x11;
    // ICW4 flags
    constexpr uint8_t ICW4_8086 = 0x01;

    void DisableLegacyPic() {
        // Remap the PIC to vectors 0xF0-0xF7 (master) and 0xF8-0xFF (slave)
        // so they don't conflict with CPU exceptions or our APIC vectors.
        // Then mask all IRQs.

        // ICW1: begin initialization sequence
        Io::Out8(ICW1_INIT, PIC1_COMMAND);
        Io::IoPortWait();
        Io::Out8(ICW1_INIT, PIC2_COMMAND);
        Io::IoPortWait();

        // ICW2: remap IRQ base offsets
        Io::Out8(0xF0, PIC1_DATA);  // Master offset
        Io::IoPortWait();
        Io::Out8(0xF8, PIC2_DATA);  // Slave offset
        Io::IoPortWait();

        // ICW3: cascade wiring
        Io::Out8(0x04, PIC1_DATA);  // Slave on IRQ2
        Io::IoPortWait();
        Io::Out8(0x02, PIC2_DATA);  // Cascade identity
        Io::IoPortWait();

        // ICW4: 8086 mode
        Io::Out8(ICW4_8086, PIC1_DATA);
        Io::IoPortWait();
        Io::Out8(ICW4_8086, PIC2_DATA);
        Io::IoPortWait();

        // Mask all IRQs on both PICs
        Io::Out8(0xFF, PIC1_DATA);
        Io::Out8(0xFF, PIC2_DATA);

        Kt::KernelLogStream(Kt::OK, "PIC") << "Legacy 8259 PIC disabled";
    }
};
