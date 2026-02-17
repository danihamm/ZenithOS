/*
    * gdt.hpp
    * Intel Global Descriptor Table
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <cstdint>

using namespace std;

namespace Hal {
    class GDTEntry {
        public:
            uint16_t LimitLow;
            uint16_t BaseLow;
            uint8_t BaseMiddle;
            uint8_t AccessByte;
            uint8_t GranularityByte;
            uint8_t BaseHigh;
        }__attribute__((packed));

        struct BasicGDT {
            GDTEntry Null;          // 0x00
            GDTEntry KernelCode;    // 0x08
            GDTEntry KernelData;    // 0x10
            GDTEntry UserData;      // 0x18  (before UserCode for SYSRET)
            GDTEntry UserCode;      // 0x20
            GDTEntry TSS;           // 0x28  (low 8 bytes of 16-byte TSS descriptor)
            GDTEntry TSSHigh;       // 0x30  (high 8 bytes of 16-byte TSS descriptor)
        }__attribute__((packed));

        struct GDTPointer {
            uint16_t Size;
            uint64_t GDTAddress;
        }__attribute__((packed));

        struct TSS64 {
            uint32_t reserved0;
            uint64_t rsp0;
            uint64_t rsp1;
            uint64_t rsp2;
            uint64_t reserved1;
            uint64_t ist1;
            uint64_t ist2;
            uint64_t ist3;
            uint64_t ist4;
            uint64_t ist5;
            uint64_t ist6;
            uint64_t ist7;
            uint64_t reserved2;
            uint16_t reserved3;
            uint16_t iopbOffset;
        }__attribute__((packed));

        extern TSS64 g_tss;

        void BridgeLoadGDT();
        void PrepareGDT();
        void LoadTSS();
};
