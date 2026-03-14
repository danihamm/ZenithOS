/*
    * AcpiSleep.cpp
    * ACPI S3 suspend-to-RAM implementation
    * Copyright (c) 2026 Daniel Hammer
*/

#include "AcpiSleep.hpp"
#include <ACPI/FADT.hpp>
#include <ACPI/AML/AmlParser.hpp>
#include <ACPI/AML/AmlInterpreter.hpp>
#include <Io/IoPort.hpp>
#include <Terminal/Terminal.hpp>
#include <CppLib/Stream.hpp>
#include <Memory/HHDM.hpp>
#include <Memory/Paging.hpp>
#include <Hal/GDT.hpp>
#include <Hal/Apic/Apic.hpp>
#include <Hal/Apic/IoApic.hpp>
#include <Hal/Apic/Pic.hpp>
#include <Timekeeping/ApicTimer.hpp>
#include <Drivers/Graphics/IntelGPU.hpp>
#include <Drivers/PS2/PS2Controller.hpp>
#include <Graphics/Cursor.hpp>
#include <Libraries/Memory.hpp>

using namespace Kt;

// Assembly routines from S3Wake.asm
extern "C" int  AcpiSaveAndSuspend(Hal::AcpiSleep::CpuState* stateArea);
extern "C" void AcpiResumeLongMode(Hal::AcpiSleep::CpuState* stateArea);
extern "C" void AcpiWakeEntry();
extern "C" void* g_wakeStatePtr;

// Real-mode trampoline from S3Trampoline.asm
extern "C" char S3TrampolineStart[];
extern "C" char S3Trampoline64[];
extern "C" char S3TrampolineData[];
extern "C" char S3TrampolineEnd[];

// Physical address where the trampoline is copied (must be < 1MB, page-aligned)
static constexpr uint32_t TRAMPOLINE_PHYS = 0x8000;

// GDT/TSS reload helpers
namespace Hal {
    extern void BridgeLoadGDT();
    extern void LoadTSS();
}

namespace Hal {
    namespace AcpiSleep {

        // ── State ───────────────────────────────────────────────────────
        static bool     g_s3Available = false;
        static uint16_t g_s3SlpTypA = 0;
        static uint16_t g_s3SlpTypB = 0;

        static uint32_t g_pm1aEventBlock  = 0;
        static uint32_t g_pm1bEventBlock  = 0;
        static uint32_t g_pm1aControlBlock = 0;
        static uint32_t g_pm1bControlBlock = 0;
        static uint8_t  g_pm1EventLength  = 0;

        static FACS*    g_facs = nullptr;

        // CPU state save area (aligned for fxsave)
        static CpuState g_cpuState __attribute__((aligned(64)));

        // ── Initialize ──────────────────────────────────────────────────
        void Initialize(ACPI::CommonSDTHeader* xsdt) {
            g_s3Available = false;

            FADT::ParsedFADT fadt{};
            if (!FADT::Parse(xsdt, fadt) || !fadt.Valid)
                return;

            // Check if FACS exists
            if (fadt.FacsAddress == 0) {
                KernelLogStream(INFO, "S3") << "No FACS - S3 unavailable";
                return;
            }

            g_facs = (FACS*)Memory::HHDM(fadt.FacsAddress);

            // Verify FACS signature
            if (g_facs->Signature[0] != 'F' || g_facs->Signature[1] != 'A' ||
                g_facs->Signature[2] != 'C' || g_facs->Signature[3] != 'S') {
                KernelLogStream(ERROR, "S3") << "Invalid FACS signature";
                g_facs = nullptr;
                return;
            }

            // Store PM register addresses
            g_pm1aEventBlock   = fadt.PM1aEventBlock;
            g_pm1bEventBlock   = fadt.PM1bEventBlock;
            g_pm1aControlBlock = fadt.PM1aControlBlock;
            g_pm1bControlBlock = fadt.PM1bControlBlock;
            g_pm1EventLength   = fadt.PM1EventLength;

            // Find \_S3_ via brute-force DSDT scan (same approach as \_S5_).
            // This works on any DSDT regardless of complexity — does not
            // require the AML interpreter or namespace to be loaded.
            auto* dsdt = (void*)Memory::HHDM(fadt.DsdtAddress);
            AML::SleepObject s3 = AML::FindSleepState(dsdt, 3);
            if (s3.Valid) {
                g_s3SlpTypA = s3.SLP_TYPa;
                g_s3SlpTypB = s3.SLP_TYPb;
                g_s3Available = true;

                KernelLogStream(OK, "S3") << "S3 suspend available (SLP_TYPa="
                    << base::hex << (uint64_t)g_s3SlpTypA
                    << " SLP_TYPb=" << base::hex << (uint64_t)g_s3SlpTypB << ")";
                KernelLogStream(INFO, "S3") << "Kernel PML4 phys = " << base::hex
                    << (uint64_t)Memory::VMM::g_paging->PML4;
            } else {
                KernelLogStream(INFO, "S3") << "\\_S3_ not found in DSDT - S3 unavailable";
            }
        }

        bool IsS3Available() {
            return g_s3Available;
        }

        // ── Evaluate _PTS (Prepare To Sleep) ────────────────────────────
        static void EvaluatePts(int sleepState) {
            auto& interp = AML::GetInterpreter();
            if (!interp.IsInitialized()) return;

            int32_t node = interp.GetNamespace().FindNode("\\_PTS");
            if (node < 0) return;

            AML::Object arg{};
            arg.Type = AML::ObjectType::Integer;
            arg.Integer = (uint64_t)sleepState;

            AML::Object result{};
            interp.EvaluateMethod("\\_PTS", &arg, 1, result);

            KernelLogStream(DEBUG, "S3") << "Evaluated \\_PTS(" << base::dec << (uint64_t)sleepState << ")";
        }

        // ── Evaluate _WAK (System Wake) ─────────────────────────────────
        static void EvaluateWak(int sleepState) {
            auto& interp = AML::GetInterpreter();
            if (!interp.IsInitialized()) return;

            int32_t node = interp.GetNamespace().FindNode("\\_WAK");
            if (node < 0) return;

            AML::Object arg{};
            arg.Type = AML::ObjectType::Integer;
            arg.Integer = (uint64_t)sleepState;

            AML::Object result{};
            interp.EvaluateMethod("\\_WAK", &arg, 1, result);

            KernelLogStream(DEBUG, "S3") << "Evaluated \\_WAK(" << base::dec << (uint64_t)sleepState << ")";
        }

        // ── Clear PM1 Status Registers ──────────────────────────────────
        static void ClearPM1Status() {
            // Write 1 to clear all status bits (write-1-to-clear semantics)
            uint16_t clearMask = PM1_WAK_STS | PM1_PWRBTN_STS | PM1_SLPBTN_STS |
                                 PM1_RTC_STS | PM1_TMR_STS | PM1_BM_STS | PM1_GBL_STS;

            if (g_pm1aEventBlock != 0)
                Io::Out16(clearMask, (uint16_t)g_pm1aEventBlock);
            if (g_pm1bEventBlock != 0)
                Io::Out16(clearMask, (uint16_t)g_pm1bEventBlock);
        }

        // ── Enable Wake Events ──────────────────────────────────────────
        static void EnableWakeEvents() {
            // Enable register is at event block + PM1EventLength/2
            uint16_t enableOffset = g_pm1EventLength / 2;

            // Enable power button and RTC as wake sources
            uint16_t enableMask = PM1_PWRBTN_EN | PM1_RTC_EN;

            if (g_pm1aEventBlock != 0 && enableOffset > 0)
                Io::Out16(enableMask, (uint16_t)(g_pm1aEventBlock + enableOffset));
            if (g_pm1bEventBlock != 0 && enableOffset > 0)
                Io::Out16(enableMask, (uint16_t)(g_pm1bEventBlock + enableOffset));
        }

        // ── Install real-mode trampoline and set waking vector ────────────
        static void SetWakingVector() {
            if (!g_facs) return;

            // Store the CpuState pointer where the 64-bit wake stub can find it.
            g_wakeStatePtr = (void*)&g_cpuState;

            // Copy the real-mode trampoline to low physical memory (0x8000).
            // This code transitions real mode → protected mode → long mode
            // and then jumps to AcpiResumeLongMode in the kernel.
            uint32_t trampolineSize = (uint32_t)(S3TrampolineEnd - S3TrampolineStart);
            uint8_t* trampolineDst = (uint8_t*)Memory::HHDM((uint64_t)TRAMPOLINE_PHYS);
            uint8_t* trampolineSrc = (uint8_t*)S3TrampolineStart;
            memcpy(trampolineDst, trampolineSrc, trampolineSize);

            // Patch the trampoline data area with CR3 and resume addresses.
            // Data layout (from S3Trampoline.asm):
            //   +0:  uint64_t CR3 (full 64-bit PML4 physical address)
            //   +8:  uint64_t CpuState virtual address
            //  +16:  uint64_t AcpiResumeLongMode virtual address
            uint32_t dataOffset = (uint32_t)(S3TrampolineData - S3TrampolineStart);
            uint8_t* dataArea = trampolineDst + dataOffset;

            // Use the kernel master PML4 (which has 0x8000 identity-mapped)
            // rather than the saved process PML4 (which doesn't).
            // AcpiResumeLongMode will restore the saved CR3 after we're
            // safely back in long mode with kernel GDT/IDT.
            uint64_t cr3val = (uint64_t)Memory::VMM::g_paging->PML4;

            // The 16-bit->32-bit trampoline path can only load 32-bit CR3.
            // If the PML4 is above 4GB, the real-mode wake path would fail.
            if (cr3val > 0xFFFFFFFF) {
                KernelLogStream(ERROR, "S3") << "PML4 at " << base::hex << cr3val
                    << " is above 4GB - real-mode wake path will fail!";
            }

            memcpy(dataArea + 0, &cr3val, 8);

            uint64_t statePtr = (uint64_t)&g_cpuState;
            memcpy(dataArea + 8, &statePtr, 8);

            uint64_t resumeAddr = (uint64_t)&AcpiResumeLongMode;
            memcpy(dataArea + 16, &resumeAddr, 8);

            // Set the 32-bit waking vector only. Setting X_FirmwareWakingVector
            // to non-zero causes this laptop's firmware to hang during wake.
            g_facs->FirmwareWakingVector = TRAMPOLINE_PHYS;
            g_facs->X_FirmwareWakingVector = 0;

            KernelLogStream(DEBUG, "S3") << "Trampoline installed at " << base::hex
                << (uint64_t)TRAMPOLINE_PHYS << " (" << base::dec
                << (uint64_t)trampolineSize << " bytes)";
            KernelLogStream(DEBUG, "S3") << "Trampoline CR3 = " << base::hex << cr3val;
        }

        // ── Wait for WAK_STS ────────────────────────────────────────────
        static void WaitForWake() {
            // After entering S3, the CPU halts. On resume, firmware runs the
            // waking vector. But if we somehow didn't enter sleep (e.g. immediate
            // wake), poll WAK_STS.
            for (int i = 0; i < 10000; i++) {
                if (g_pm1aEventBlock != 0) {
                    uint16_t sts = Io::In16((uint16_t)g_pm1aEventBlock);
                    if (sts & PM1_WAK_STS) return;
                }
                Io::IoPortWait();
            }
        }

        // ── Suspend ─────────────────────────────────────────────────────
        int Suspend() {
            if (!g_s3Available) {
                KernelLogStream(ERROR, "S3") << "S3 suspend not available";
                return -1;
            }

            KernelLogStream(INFO, "S3") << "Preparing for S3 suspend...";

            // 1. Evaluate _PTS(3) — Prepare To Sleep
            EvaluatePts(3);

            // 2. Save CPU state. AcpiSaveAndSuspend returns 1 on initial call,
            //    and 0 when we resume from S3 (AcpiResumeLongMode sets RAX=0).
            int resumed = !AcpiSaveAndSuspend(&g_cpuState);
            if (resumed) {
                // ── RESUME PATH ─────────────────────────────────────────
                // We just woke from S3. Firmware ran our waking vector which
                // called AcpiResumeLongMode, restoring all registers and
                // returning here with 0.

                // Debug: write a green rectangle to the top-left corner of the
                // framebuffer. The framebuffer memory survives S3 (it's RAM).
                // This will be visible once the GPU display plane is restored,
                // confirming the CPU successfully completed the resume path.
                {
                    uint32_t* fb = ::Graphics::Cursor::GetFramebufferBase();
                    uint64_t pitch = ::Graphics::Cursor::GetFramebufferPitch();
                    if (fb && pitch > 0) {
                        for (int y = 0; y < 32; y++) {
                            uint32_t* row = (uint32_t*)((uint8_t*)fb + y * pitch);
                            for (int x = 0; x < 32; x++) {
                                row[x] = 0xFF00FF00; // Green (ARGB)
                            }
                        }
                    }
                }

                // Reload GDT and TSS (firmware may have clobbered them)
                Hal::BridgeLoadGDT();
                Hal::LoadTSS();

                // Re-disable legacy 8259 PIC. Firmware may have re-enabled
                // it during S3 resume POST, which could cause spurious
                // interrupts on conflicting vectors once we enable interrupts.
                Hal::DisableLegacyPic();

                // Re-enable the Local APIC (MSR global enable + SVR + TPR)
                Hal::LocalApic::Reinitialize();

                // Restore I/O APIC redirection entries (lost during S3).
                // Must be done before enabling interrupts so IRQ routing
                // is in place when devices start generating interrupts.
                Hal::IoApic::Reinitialize();

                // Restart the APIC timer
                Timekeeping::ApicTimerReinitialize();

                // Clear WAK_STS
                ClearPM1Status();

                // Evaluate _WAK(3)
                EvaluateWak(3);

                // Re-enable PS/2 controller (ports and interrupts may be
                // disabled after S3; skip full self-test to avoid resetting
                // attached devices)
                Drivers::PS2::Reinitialize();

                // Restore Intel GPU display (GTT + display plane lost during S3)
                if (Drivers::Graphics::IntelGPU::IsInitialized()) {
                    Drivers::Graphics::IntelGPU::Reinitialize();
                }

                // Re-enable interrupts
                asm volatile("sti");

                KernelLogStream(OK, "S3") << "Resumed from S3 suspend";

                return 0;
            }

            // ── SUSPEND PATH (initial save returned 1) ──────────────────

            // 3. Disable interrupts
            asm volatile("cli");

            // 4. Identity-map the trampoline page so the CPU can execute it
            //    after paging is re-enabled with our CR3 during wake.
            //    Physical 0x8000 → virtual 0x8000 (flat identity mapping).
            Memory::VMM::g_paging->Map(TRAMPOLINE_PHYS, TRAMPOLINE_PHYS);

            // 5. Flush all caches
            asm volatile("wbinvd");

            // 6. Set the waking vector in FACS
            SetWakingVector();

            // 7. Clear any pending PM1 status bits
            ClearPM1Status();

            // 8. Enable wake events (power button, RTC)
            EnableWakeEvents();

            // 9. Write SLP_TYP to PM1 control registers (without SLP_EN)
            uint16_t pm1a = Io::In16((uint16_t)g_pm1aControlBlock);
            pm1a = (pm1a & ~PM1_SLP_TYP_MASK) | (g_s3SlpTypA << 10);
            Io::Out16(pm1a, (uint16_t)g_pm1aControlBlock);

            if (g_pm1bControlBlock != 0) {
                uint16_t pm1b = Io::In16((uint16_t)g_pm1bControlBlock);
                pm1b = (pm1b & ~PM1_SLP_TYP_MASK) | (g_s3SlpTypB << 10);
                Io::Out16(pm1b, (uint16_t)g_pm1bControlBlock);
            }

            // 10. Flush caches again
            asm volatile("wbinvd");

            // 11. Assert SLP_EN — this triggers S3 entry
            Io::Out16(pm1a | PM1_SLP_EN, (uint16_t)g_pm1aControlBlock);

            if (g_pm1bControlBlock != 0) {
                uint16_t pm1b = Io::In16((uint16_t)g_pm1bControlBlock);
                Io::Out16(pm1b | PM1_SLP_EN, (uint16_t)g_pm1bControlBlock);
            }

            // The CPU must halt for the chipset/hypervisor to complete the S3
            // transition. On real hardware the chipset cuts power after SLP_EN;
            // on QEMU/KVM the vCPU must reach HLT before QEMU finalizes S3.
            // On resume, firmware jumps to our waking vector.
            for (;;) {
                asm volatile("hlt");
                // If we wake from HLT (spurious or real wake), check WAK_STS
                if (g_pm1aEventBlock != 0) {
                    uint16_t sts = Io::In16((uint16_t)g_pm1aEventBlock);
                    if (sts & PM1_WAK_STS) break;
                }
            }

            // If we get here, either S3 failed or we woke immediately.
            // The normal wake path goes through AcpiResumeLongMode instead.
            // Re-enable interrupts as a safety measure.
            asm volatile("sti");

            KernelLogStream(WARNING, "S3") << "S3 entry may have failed (fell through)";
            return 0;
        }

    };
};
