/*
    * AcpiSleep.hpp
    * ACPI S3 suspend-to-RAM support
    * Copyright (c) 2026 Daniel Hammer
*/

#pragma once
#include <cstdint>
#include <ACPI/ACPI.hpp>

namespace Hal {
    namespace AcpiSleep {

        // ── FACS (Firmware ACPI Control Structure) ──────────────────────
        struct FACS {
            char     Signature[4];       // "FACS"
            uint32_t Length;
            uint32_t HardwareSignature;
            uint32_t FirmwareWakingVector;   // 32-bit real-mode wake address
            uint32_t GlobalLock;
            uint32_t Flags;
            uint64_t X_FirmwareWakingVector; // 64-bit wake address (ACPI 2.0+)
            uint8_t  Version;
            uint8_t  Reserved[3];
            uint32_t OspmFlags;
        } __attribute__((packed));

        // FACS Flags
        static constexpr uint32_t FACS_S4BIOS_F        = (1 << 0);
        static constexpr uint32_t FACS_64BIT_WAKE_F    = (1 << 1);

        // FADT Flags relevant to sleep
        static constexpr uint32_t FADT_S4_RTC_STS_VALID = (1 << 16);
        static constexpr uint32_t FADT_HW_REDUCED_ACPI  = (1 << 20);

        // ── PM1 Status Register Bits ────────────────────────────────────
        static constexpr uint16_t PM1_TMR_STS   = (1 << 0);
        static constexpr uint16_t PM1_BM_STS    = (1 << 4);
        static constexpr uint16_t PM1_GBL_STS   = (1 << 5);
        static constexpr uint16_t PM1_PWRBTN_STS = (1 << 8);
        static constexpr uint16_t PM1_SLPBTN_STS = (1 << 9);
        static constexpr uint16_t PM1_RTC_STS   = (1 << 10);
        static constexpr uint16_t PM1_WAK_STS   = (1 << 15);

        // ── PM1 Enable Register Bits ────────────────────────────────────
        static constexpr uint16_t PM1_TMR_EN    = (1 << 0);
        static constexpr uint16_t PM1_GBL_EN    = (1 << 5);
        static constexpr uint16_t PM1_PWRBTN_EN = (1 << 8);
        static constexpr uint16_t PM1_SLPBTN_EN = (1 << 9);
        static constexpr uint16_t PM1_RTC_EN    = (1 << 10);

        // ── PM1 Control Register Bits ───────────────────────────────────
        static constexpr uint16_t PM1_SCI_EN    = (1 << 0);
        static constexpr uint16_t PM1_BM_RLD    = (1 << 1);
        static constexpr uint16_t PM1_SLP_TYP_MASK = 0x1C00; // bits 10-12
        static constexpr uint16_t PM1_SLP_EN    = (1 << 13);

        // ── CPU State (saved across S3) ─────────────────────────────────
        // Layout must match S3Wake.asm offsets exactly.
        // No FPU/SSE state — the kernel is compiled with -mno-sse.
        struct CpuState {
            uint64_t Rax, Rbx, Rcx, Rdx;   // 0x00 - 0x18
            uint64_t Rsi, Rdi, Rbp, Rsp;   // 0x20 - 0x38
            uint64_t R8, R9, R10, R11;     // 0x40 - 0x58
            uint64_t R12, R13, R14, R15;   // 0x60 - 0x78
            uint64_t Rflags;               // 0x80
            uint64_t Rip;                  // 0x88
            uint64_t Cr3;                  // 0x90
            uint64_t Cr0, Cr4;             // 0x98, 0xA0
            uint8_t  GdtPtr[10];           // 0xA8
            uint8_t  IdtPtr[10];           // 0xB2
        } __attribute__((aligned(16)));

        // ── Sleep API ───────────────────────────────────────────────────

        // Initialize sleep support. Called during boot after ACPI init.
        // xsdt is used to read FADT for FACS and PM register addresses.
        void Initialize(ACPI::CommonSDTHeader* xsdt);

        // Returns true if S3 suspend is supported by the hardware.
        bool IsS3Available();

        // Perform an S3 suspend-to-RAM.
        // Saves CPU state, evaluates _PTS(3), programs PM registers,
        // sets waking vector, and enters S3.
        // Returns 0 on successful resume, or -1 if S3 is unavailable.
        int Suspend();

    };
};
