/*
    * AcpiSleep.cpp
    * ACPI S3 suspend-to-RAM implementation
    * Copyright (c) 2026 Daniel Hammer
*/

#include "AcpiSleep.hpp"
#include "AcpiEvents.hpp"
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
#include <Hal/MSR.hpp>
#include <Hal/SmpBoot.hpp>
#include <Api/Syscall.hpp>

using namespace Kt;

// Assembly routines from S3Wake.asm
extern "C" int  AcpiSaveAndSuspend(Hal::AcpiSleep::CpuState* stateArea);
extern "C" void AcpiResumeLongMode(Hal::AcpiSleep::CpuState* stateArea);
extern "C" void AcpiWakeEntry();
extern "C" void* g_wakeStatePtr;

// Called directly by AcpiResumeLongMode after restoring CPU state.
// This avoids returning to Suspend() via ret, which is fragile because
// the compiler's stack frame expectations may not match the restored state.
extern "C" void AcpiResumeEntry();

// Real-mode trampoline from S3Trampoline.asm
extern "C" char S3TrampolineStart[];
extern "C" char S3Trampoline64[];
extern "C" char S3TrampolineData[];
extern "C" char S3TrampolineEnd[];

// Physical address where the trampoline is copied (must be < 1MB, page-aligned)
static constexpr uint32_t TRAMPOLINE_PHYS = 0x8000;

// Physical address for the shadow PML4 used by the trampoline (must be < 4GB).
// The kernel PML4 may be above 4GB, but the 32-bit trampoline can only load
// a 32-bit CR3. We copy the kernel PML4 entries here before entering S3.
static constexpr uint32_t SHADOW_PML4_PHYS = 0x9000;

// GDT/TSS reload helpers
namespace Hal {
    extern void BridgeLoadGDT();
    extern void LoadTSS();
}

namespace Hal {
    namespace AcpiSleep {

        // ============================================================================

        // State

        // ============================================================================
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

        // Set by AcpiResumeEntry to signal Suspend() that resume completed.
        static volatile int g_resumeComplete = 0;

        // ============================================================================

        // Initialize

        // ============================================================================
        void Initialize(ACPI::CommonSDTHeader* xsdt) {
            g_s3Available = false;

            // Check CMOS 0x72 for S3 wake progress from a previous attempt.
            // Non-zero means the last wake attempt reached a specific stage.
            Io::Out8(0xF2, 0x70);  // CMOS register 0x72 | NMI disable
            uint8_t wakeProgress = Io::In8(0x71);
            // Check CMOS 0x73 — set to 0xDD if C resume code was ever reached
            // (survives trampoline overwrites of 0x72 during sleep-wake loops)
            Io::Out8(0xF3, 0x70);
            uint8_t cReached = Io::In8(0x71);
            if (wakeProgress != 0 || cReached != 0) {
                KernelLogStream(WARNING, "S3") << "Previous S3 wake progress: 0x"
                    << base::hex << (uint64_t)wakeProgress
                    << (wakeProgress == 0xA1 ? " (16-bit trampoline)" :
                        wakeProgress == 0xA2 ? " (32-bit mode)" :
                        wakeProgress == 0xA3 ? " (entering long mode)" :
                        wakeProgress == 0xB1 ? " (64-bit UEFI entry)" :
                        wakeProgress == 0xC1 ? " (AcpiResumeLongMode entry)" :
                        wakeProgress == 0xC2 ? " (GDT+CS reloaded)" :
                        wakeProgress == 0xC3 ? " (IDT+CR4/CR0/CR3 restored)" :
                        wakeProgress == 0xC4 ? " (about to ret to Suspend)" :
                        wakeProgress == 0xD1 ? " (C resume path)" : " (unknown)");
                if (cReached == 0xEE)
                    KernelLogStream(WARNING, "S3") << "Full resume completed (all reinit done)";
                else if (cReached == 0xDD)
                    KernelLogStream(WARNING, "S3") << "C resume entered but did NOT complete all reinit";
                else
                    KernelLogStream(WARNING, "S3") << "C resume path was NOT reached";
                // Read reinit step progress from CMOS 0x74
                Io::Out8(0xF4, 0x70);
                uint8_t reinitStep = Io::In8(0x71);
                if (reinitStep != 0) {
                    const char* stepNames[] = {
                        "?", "GDT", "TSS", "Syscalls", "PAT", "PIC",
                        "LocalAPIC", "IoAPIC", "APICTimer", "PM1Clear",
                        "WAK", "PS2", "IntelGPU", "sti"
                    };
                    const char* stepName = reinitStep <= 0x0D ? stepNames[reinitStep] : "?";
                    KernelLogStream(WARNING, "S3") << "Reinit stopped after step 0x"
                        << base::hex << (uint64_t)reinitStep << " (" << stepName
                        << ") - next step crashed/hung";
                }
                Io::Out8(0xF4, 0x70);
                Io::Out8(0x00, 0x71);
                // Clear both
                Io::Out8(0xF2, 0x70);
                Io::Out8(0x00, 0x71);
                Io::Out8(0xF3, 0x70);
                Io::Out8(0x00, 0x71);
            }

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

            // Find \_S3_ via brute-force DSDT scan. The AML interpreter stores
            // packages as raw bytecode (no structured element access yet), so
            // the brute-force scanner is the reliable path for now.
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
                KernelLogStream(INFO, "S3") << "FACS at phys " << base::hex
                    << (uint64_t)fadt.FacsAddress << " version=" << base::dec
                    << (uint64_t)g_facs->Version << " flags=" << base::hex
                    << (uint64_t)g_facs->Flags
                    << (g_facs->Flags & FACS_64BIT_WAKE_F ? " (64BIT_WAKE)" : " (32BIT_WAKE only)");
            } else {
                KernelLogStream(INFO, "S3") << "\\_S3_ not found in DSDT - S3 unavailable";
            }
        }

        bool IsS3Available() {
            return g_s3Available;
        }

        // ============================================================================

        // Evaluate _PTS (Prepare To Sleep)

        // ============================================================================
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

        // ============================================================================

        // Evaluate _WAK (System Wake)

        // ============================================================================
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

        // ============================================================================

        // Clear PM1 Status Registers

        // ============================================================================
        static void ClearPM1Status() {
            // Write 1 to clear all status bits (write-1-to-clear semantics)
            uint16_t clearMask = PM1_WAK_STS | PM1_PWRBTN_STS | PM1_SLPBTN_STS |
                                 PM1_RTC_STS | PM1_TMR_STS | PM1_BM_STS | PM1_GBL_STS;

            if (g_pm1aEventBlock != 0)
                Io::Out16(clearMask, (uint16_t)g_pm1aEventBlock);
            if (g_pm1bEventBlock != 0)
                Io::Out16(clearMask, (uint16_t)g_pm1bEventBlock);
        }

        // ============================================================================

        // Enable Wake Events

        // ============================================================================
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

        // ============================================================================

        // Install real-mode trampoline and set waking vector

        // ============================================================================
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

            // Patch the trampoline data area with CR3, resume addresses,
            // and PM1 control port addresses.
            // Data layout (from S3Trampoline.asm):
            //   +0:  uint64_t CR3 (full 64-bit PML4 physical address)
            //   +8:  uint64_t CpuState virtual address
            //  +16:  uint64_t AcpiResumeLongMode virtual address
            //  +24:  uint16_t PM1a control block I/O port
            //  +26:  uint16_t PM1b control block I/O port
            uint32_t dataOffset = (uint32_t)(S3TrampolineData - S3TrampolineStart);
            uint8_t* dataArea = trampolineDst + dataOffset;

            // The kernel PML4 may be above 4GB (e.g. at 0x8BF7CC000), but
            // the 32-bit trampoline can only load a 32-bit CR3. Create a
            // shadow copy of the PML4 at a fixed low-memory address.
            // Only the PML4 page itself needs to be below 4GB — its entries
            // contain full 52-bit physical addresses for sub-tables.
            uint64_t kernelPml4Phys = (uint64_t)Memory::VMM::g_paging->PML4;
            uint8_t* kernelPml4Virt = (uint8_t*)Memory::HHDM(kernelPml4Phys);
            uint8_t* shadowPml4Virt = (uint8_t*)Memory::HHDM((uint64_t)SHADOW_PML4_PHYS);
            memcpy(shadowPml4Virt, kernelPml4Virt, 4096);

            uint64_t cr3val = (uint64_t)SHADOW_PML4_PHYS;

            KernelLogStream(DEBUG, "S3") << "Shadow PML4 at " << base::hex << cr3val
                << " (kernel PML4 at " << base::hex << kernelPml4Phys << ")";

            memcpy(dataArea + 0, &cr3val, 8);

            uint64_t statePtr = (uint64_t)&g_cpuState;
            memcpy(dataArea + 8, &statePtr, 8);

            uint64_t resumeAddr = (uint64_t)&AcpiResumeLongMode;
            memcpy(dataArea + 16, &resumeAddr, 8);

            uint16_t pm1a = (uint16_t)g_pm1aControlBlock;
            uint16_t pm1b = (uint16_t)g_pm1bControlBlock;
            memcpy(dataArea + 24, &pm1a, 2);
            memcpy(dataArea + 26, &pm1b, 2);

            // Always set the 32-bit real-mode waking vector (universal fallback).
            g_facs->FirmwareWakingVector = TRAMPOLINE_PHYS;

            // Use the 64-bit waking vector if the firmware advertises support.
            // FACS flags bit 1 (64BIT_WAKE_F) indicates the platform can
            // transfer control in 64-bit mode.
            if (g_facs->Flags & FACS_64BIT_WAKE_F) {
                g_facs->X_FirmwareWakingVector = TRAMPOLINE_PHYS + 0x100;
                KernelLogStream(DEBUG, "S3") << "Using 64-bit waking vector (0x8100)";
            } else {
                g_facs->X_FirmwareWakingVector = 0;
                KernelLogStream(DEBUG, "S3") << "Using 32-bit waking vector (0x8000)";
            }

            KernelLogStream(DEBUG, "S3") << "Trampoline installed at " << base::hex
                << (uint64_t)TRAMPOLINE_PHYS << " (" << base::dec
                << (uint64_t)trampolineSize << " bytes)";
            KernelLogStream(DEBUG, "S3") << "Trampoline CR3 = " << base::hex << cr3val;
        }

        // ============================================================================

        // Wait for WAK_STS

        // ============================================================================
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

        // ============================================================================

        // Resume Entry (called directly by AcpiResumeLongMode)

        // ============================================================================
        // This is a standalone function with its own stack frame, avoiding
        // any dependency on the compiler's layout of Suspend().
        extern "C" void AcpiResumeEntry() {
            // Progress: reached C resume path (0xD1)
            Io::Out8(0xF2, 0x70);
            Io::Out8(0xD1, 0x71);
            Io::Out8(0xF3, 0x70);
            Io::Out8(0xDD, 0x71);

            // CRITICAL: Clear SLP_EN and SLP_TYP in PM1 control registers
            if (g_pm1aControlBlock != 0) {
                uint16_t pm1a = Io::In16((uint16_t)g_pm1aControlBlock);
                pm1a &= ~(PM1_SLP_EN | PM1_SLP_TYP_MASK);
                Io::Out16(pm1a, (uint16_t)g_pm1aControlBlock);
            }
            if (g_pm1bControlBlock != 0) {
                uint16_t pm1b = Io::In16((uint16_t)g_pm1bControlBlock);
                pm1b &= ~(PM1_SLP_EN | PM1_SLP_TYP_MASK);
                Io::Out16(pm1b, (uint16_t)g_pm1bControlBlock);
            }

            // Debug: green rectangle on framebuffer
            {
                uint32_t* fb = ::Graphics::Cursor::GetFramebufferBase();
                uint64_t pitch = ::Graphics::Cursor::GetFramebufferPitch();
                if (fb && pitch > 0) {
                    for (int y = 0; y < 32; y++) {
                        uint32_t* row = (uint32_t*)((uint8_t*)fb + y * pitch);
                        for (int x = 0; x < 32; x++) {
                            row[x] = 0xFF00FF00;
                        }
                    }
                }
            }

            // Use CMOS 0x74 as step-by-step progress through reinit
            auto reinitProgress = [](uint8_t step) {
                Io::Out8(0xF4, 0x70);
                Io::Out8(step, 0x71);
            };

            reinitProgress(0x01);  // GDT
            Hal::BridgeLoadGDT();
            reinitProgress(0x02);  // TSS
            Hal::LoadTSS();
            reinitProgress(0x03);  // Syscalls
            Montauk::InitializeSyscalls();
            // Re-set GS base for BSP (lost during S3)
            {
                auto* bsp = Smp::GetCpuData(0);
                if (bsp) {
                    Hal::WriteMSR(0xC0000101, (uint64_t)bsp); // IA32_GS_BASE
                    Hal::WriteMSR(0xC0000102, 0);              // IA32_KERNEL_GS_BASE
                }
            }
            reinitProgress(0x04);  // PAT
            Hal::InitializePAT();
            reinitProgress(0x05);  // PIC
            Hal::DisableLegacyPic();
            reinitProgress(0x06);  // Local APIC
            Hal::LocalApic::Reinitialize();
            reinitProgress(0x07);  // IO APIC
            Hal::IoApic::Reinitialize();
            reinitProgress(0x08);  // APIC Timer
            Timekeeping::ApicTimerReinitialize();
            reinitProgress(0x09);  // PM1 clear
            ClearPM1Status();
            reinitProgress(0x0A);  // _WAK
            EvaluateWak(3);
            reinitProgress(0x0B);  // PS/2
            Drivers::PS2::Reinitialize();
            // Re-enable ACPI events (power button) after S3
            AcpiEvents::Reinitialize();
            reinitProgress(0x0C);  // Intel GPU
            if (Drivers::Graphics::IntelGPU::IsInitialized()) {
                Drivers::Graphics::IntelGPU::Reinitialize();
            }
            reinitProgress(0x0D);  // sti
            asm volatile("sti");

            KernelLogStream(OK, "S3") << "Resumed from S3 suspend";

            // Progress: all reinit complete (0xEE in CMOS 0x73)
            Io::Out8(0xF3, 0x70);
            Io::Out8(0xEE, 0x71);

            // Signal Suspend() that resume is complete. AcpiResumeEntry
            // was entered via jmp (not call), so our ret will pop the
            // original return address from call AcpiSaveAndSuspend,
            // returning to Suspend() which checks this flag.
            g_resumeComplete = 1;
        }

        // ============================================================================

        // Suspend

        // ============================================================================
        int Suspend() {
            if (!g_s3Available) {
                KernelLogStream(ERROR, "S3") << "S3 suspend not available";
                return -1;
            }

            KernelLogStream(INFO, "S3") << "Preparing for S3 suspend...";

            // 1. Evaluate _PTS(3) — Prepare To Sleep
            EvaluatePts(3);

            // 2. Save CPU state. On resume, AcpiResumeLongMode jumps to
            //    AcpiResumeEntry (not back here), which does all reinit and
            //    sets g_resumeComplete=1, then returns here via the stack.
            g_resumeComplete = 0;
            AcpiSaveAndSuspend(&g_cpuState);

            // If we get here after resume, AcpiResumeEntry already ran.
            if (g_resumeComplete) {
                return 0;
            }

            // ============================================================================

            // SUSPEND PATH (initial save returned 1)

            // ============================================================================
            // Clear CMOS progress markers before entering S3.
            Io::Out8(0xF2, 0x70);  // CMOS register 0x72 | NMI disable
            Io::Out8(0x00, 0x71);
            Io::Out8(0xF3, 0x70);  // CMOS register 0x73
            Io::Out8(0x00, 0x71);

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
