/*
    * IDT.hpp
    * Intel Interrupt Descriptor Table implementation
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <cstdint>
#include <cstddef>

namespace Hal {
    struct InterruptDescriptor {
        uint16_t Offset1;
        uint16_t Selector;
        uint8_t IST;
        uint8_t TypeAttributes;
        uint16_t Offset2;
        uint32_t Offset3;
        uint32_t Zero;
    }__attribute__((packed));

    struct IDTRStruct {
        uint16_t Limit;
        uint64_t Base;
    }__attribute__((packed));

    void IDTInitialize();
    void IDTEncodeInterrupt(std::size_t i, void* handler, uint8_t type_attr);
    void IDTReload();
};