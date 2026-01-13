/*
    * gdt.hpp
    * Intel Global Descriptor Table
    * Copyright (c) 2025 Daniel Hammer
*/

#include "GDT.hpp"
#include <Gui/DebugGui.hpp>


namespace Hal {

    GDTPointer gdtPointer{};
    BasicGDT kernelGDT{};
    
    void PrepareGDT() {
        kernelGDT = {
            {0xFFFF, 0, 0, 0x00, 0x00, 0},    
            {0xFFFF, 0, 0, 0x9A, 0xA0, 0},
            {0xFFFF, 0, 0, 0x92, 0xA0, 0},    
            {0xFFFF, 0, 0, 0x9A, 0xA0, 0},
            {0xFFFF, 0, 0, 0x92, 0xA0, 0},

            {0, 0, 0, 0xFA, 0x00, 0x0},
        };
    
        gdtPointer = GDTPointer{
            .Size = sizeof(kernelGDT) - 1,
            .GDTAddress = (uint64_t)&kernelGDT
        };
    }
    
    // Helpers implemented in gdt.asm
    extern "C" void LoadGDT(GDTPointer *gdtPointer);
    extern "C" void ReloadSegments();
    
    void BridgeLoadGDT() {
        LoadGDT(&gdtPointer);
        ReloadSegments();

        Gui::GuiLogStream(Gui::LogLevel::Debug, "Hal") << "Set new GDT (0x" << Gui::base::hex() << (uint64_t)&kernelGDT << ")";
    }
};