/*
    * SmpBoot.cpp
    * Symmetric Multiprocessing bootstrap and AP entry
    * Copyright (c) 2026 Daniel Hammer
*/

#include "SmpBoot.hpp"
#include <Hal/Apic/Apic.hpp>
#include <Hal/Apic/Interrupts.hpp>
#include <Hal/IDT.hpp>
#include <Hal/MSR.hpp>
#include <Hal/Cpu.hpp>
#include <Memory/Paging.hpp>
#include <Memory/HHDM.hpp>
#include <Terminal/Terminal.hpp>
#include <CppLib/Stream.hpp>
#include <Timekeeping/ApicTimer.hpp>
#include <limine.h>

// Defined in Platform/Limine.hpp (included only by Main.cpp to avoid
// duplicating the LIMINE_BASE_REVISION tag).
extern volatile limine_mp_request mp_request;
#include <Libraries/Memory.hpp>

// Verify assembly offsets match struct layout
static_assert(__builtin_offsetof(Smp::CpuData, selfPtr)       == CPUDATA_SELF_PTR,     "CpuData offset mismatch: selfPtr");
static_assert(__builtin_offsetof(Smp::CpuData, kernelRsp)     == CPUDATA_KERNEL_RSP,   "CpuData offset mismatch: kernelRsp");
static_assert(__builtin_offsetof(Smp::CpuData, userRspScratch)== CPUDATA_USER_RSP,     "CpuData offset mismatch: userRspScratch");
static_assert(__builtin_offsetof(Smp::CpuData, currentSlot)   == CPUDATA_CURRENT_SLOT, "CpuData offset mismatch: currentSlot");

extern "C" void SyscallEntry();

// Assembly helpers (GDT.asm)
extern "C" void LoadGDT(Hal::GDTPointer* ptr);
extern "C" void ReloadSegments();
extern "C" void LoadTR();

using namespace Kt;

namespace Smp {

    static CpuData g_cpus[MaxCPUs];
    static int g_cpuCount = 0;

    CpuData* GetCpuData(int index) {
        if (index < 0 || index >= g_cpuCount) return nullptr;
        return &g_cpus[index];
    }

    int GetCpuCount() {
        return g_cpuCount;
    }

    // ====================================================================
    // Per-CPU GDT/TSS setup
    // ====================================================================

    static void SetupPerCpuGdtTss(CpuData& cpu) {
        // Zero the TSS
        memset(&cpu.cpuTss, 0, sizeof(Hal::TSS64));
        cpu.cpuTss.iopbOffset = sizeof(Hal::TSS64);

        // Copy the standard GDT layout
        cpu.cpuGdt = {
            {0xFFFF, 0, 0, 0x00, 0x00, 0},    // 0x00 Null
            {0xFFFF, 0, 0, 0x9A, 0xA0, 0},    // 0x08 KernelCode
            {0xFFFF, 0, 0, 0x92, 0xA0, 0},    // 0x10 KernelData
            {0xFFFF, 0, 0, 0xF2, 0xA0, 0},    // 0x18 UserData
            {0xFFFF, 0, 0, 0xFA, 0xA0, 0},    // 0x20 UserCode
            {0, 0, 0, 0, 0, 0},               // 0x28 TSS low
            {0, 0, 0, 0, 0, 0},               // 0x30 TSS high
        };

        // Encode the 16-byte TSS descriptor
        uint64_t base = (uint64_t)&cpu.cpuTss;
        uint32_t limit = sizeof(Hal::TSS64) - 1;

        // Low 8 bytes
        cpu.cpuGdt.TSS.LimitLow       = limit & 0xFFFF;
        cpu.cpuGdt.TSS.BaseLow        = base & 0xFFFF;
        cpu.cpuGdt.TSS.BaseMiddle      = (base >> 16) & 0xFF;
        cpu.cpuGdt.TSS.AccessByte      = 0x89;  // Present, 64-bit TSS Available
        cpu.cpuGdt.TSS.GranularityByte = (limit >> 16) & 0x0F;
        cpu.cpuGdt.TSS.BaseHigh        = (base >> 24) & 0xFF;

        // High 8 bytes (base[63:32])
        uint32_t baseUpper = (uint32_t)(base >> 32);
        cpu.cpuGdt.TSSHigh.LimitLow       = baseUpper & 0xFFFF;
        cpu.cpuGdt.TSSHigh.BaseLow        = (baseUpper >> 16) & 0xFFFF;
        cpu.cpuGdt.TSSHigh.BaseMiddle      = 0;
        cpu.cpuGdt.TSSHigh.AccessByte      = 0;
        cpu.cpuGdt.TSSHigh.GranularityByte = 0;
        cpu.cpuGdt.TSSHigh.BaseHigh        = 0;

        cpu.tss = &cpu.cpuTss;
    }

    // ====================================================================
    // Set GS base MSRs for per-CPU data
    // ====================================================================

    static constexpr uint32_t IA32_GS_BASE        = 0xC0000101;
    static constexpr uint32_t IA32_KERNEL_GS_BASE = 0xC0000102;

    static void SetGSBase(CpuData* cpu) {
        // In kernel mode, GSBASE = per-CPU data pointer.
        // KernelGSBASE = 0 (user GS base, swapped in on swapgs).
        Hal::WriteMSR(IA32_GS_BASE, (uint64_t)cpu);
        Hal::WriteMSR(IA32_KERNEL_GS_BASE, 0);
    }

    // ====================================================================
    // BSP initialization
    // ====================================================================

    void InitBsp() {
        memset(g_cpus, 0, sizeof(g_cpus));

        CpuData& bsp = g_cpus[0];
        bsp.selfPtr = (uint64_t)&bsp;
        bsp.cpuIndex = 0;
        bsp.lapicId = Hal::LocalApic::GetId();
        bsp.currentSlot = -1;
        bsp.started = true;

        // BSP uses the global TSS (already set up in PrepareGDT)
        bsp.tss = &Hal::g_tss;

        // Set GS base for BSP
        SetGSBase(&bsp);

        g_cpuCount = 1;

        KernelLogStream(OK, "SMP") << "BSP initialized (LAPIC ID " << (uint64_t)bsp.lapicId << ")";
    }

    // ====================================================================
    // AP entry point
    // Called by Limine when goto_address is written.
    // RDI = pointer to limine_mp_info for this CPU.
    // Runs on a 64KiB Limine-provided stack.
    // ====================================================================

    static void ApEntry(limine_mp_info* info) {
        // Find our CpuData (stored in extra_argument by BootAPs)
        CpuData* cpu = (CpuData*)info->extra_argument;

        // --- Load per-CPU GDT ---
        Hal::GDTPointer gdtPtr {
            .Size = sizeof(Hal::BasicGDT) - 1,
            .GDTAddress = (uint64_t)&cpu->cpuGdt,
        };
        LoadGDT(&gdtPtr);
        ReloadSegments();

        // --- Load TSS ---
        LoadTR();

        // --- Load shared IDT ---
        Hal::IDTReload();

        // --- Switch to kernel page tables ---
        Memory::VMM::LoadCR3(Memory::VMM::g_paging->PML4);

        // --- Enable SSE ---
        Hal::EnableSSE();

        // --- Set GS base ---
        SetGSBase(cpu);

        // --- Initialize local APIC ---
        Hal::LocalApic::InitializeAP();

        // --- Program SYSCALL MSRs ---
        uint64_t efer = Hal::ReadMSR(Hal::IA32_EFER);
        efer |= 1;  // SCE
        Hal::WriteMSR(Hal::IA32_EFER, efer);

        uint64_t star = (0x0010ULL << 48) | (0x0008ULL << 32);
        Hal::WriteMSR(Hal::IA32_STAR, star);
        Hal::WriteMSR(Hal::IA32_LSTAR, (uint64_t)SyscallEntry);
        Hal::WriteMSR(Hal::IA32_FMASK, 0x200);

        // --- Program PAT (entry 1 = WC) ---
        Hal::InitializePAT();

        // --- Calibrate and start APIC timer ---
        Timekeeping::ApicTimerInitializeAP();

        // --- Signal that we are online ---
        cpu->started = true;

        // --- Enable interrupts and enter idle loop ---
        asm volatile("sti");

        for (;;) {
            asm volatile("hlt");
        }
    }

    // ====================================================================
    // Boot all APs
    // ====================================================================

    void BootAPs() {
        if (mp_request.response == nullptr) {
            KernelLogStream(WARNING, "SMP") << "No MP response from bootloader - single CPU mode";
            return;
        }

        auto* resp = mp_request.response;
        uint64_t cpuCount = resp->cpu_count;

        KernelLogStream(INFO, "SMP") << "Bootloader reports " << cpuCount << " CPU(s), BSP LAPIC ID "
            << (uint64_t)resp->bsp_lapic_id;

        if (cpuCount <= 1) {
            KernelLogStream(INFO, "SMP") << "Single CPU system - no APs to boot";
            return;
        }

        if (cpuCount > (uint64_t)MaxCPUs) {
            KernelLogStream(WARNING, "SMP") << "Clamping CPU count from " << cpuCount << " to " << (uint64_t)MaxCPUs;
            cpuCount = MaxCPUs;
        }

        // Prepare all APs, then wake them all at once. This is safe because:
        // - APs use BSP's timer calibration (no PIT contention)
        // - APs don't log (no terminal contention)
        // - Each AP's init is purely local (GDT, TSS, APIC, MSRs)
        int apIndex = 1;  // BSP is index 0
        for (uint64_t i = 0; i < cpuCount; i++) {
            limine_mp_info* info = resp->cpus[i];

            if (info->lapic_id == resp->bsp_lapic_id) continue;
            if (apIndex >= MaxCPUs) break;

            CpuData& ap = g_cpus[apIndex];
            ap.selfPtr = (uint64_t)&ap;
            ap.cpuIndex = apIndex;
            ap.lapicId = info->lapic_id;
            ap.currentSlot = -1;
            ap.started = false;

            SetupPerCpuGdtTss(ap);
            info->extra_argument = (uint64_t)&ap;

            // Wake this AP (it runs ApEntry in parallel with other APs)
            __atomic_store_n(&info->goto_address, (limine_goto_address)ApEntry, __ATOMIC_SEQ_CST);

            apIndex++;
        }

        g_cpuCount = apIndex;

        // Wait for all APs to come online
        for (int i = 1; i < g_cpuCount; i++) {
            volatile bool* flag = &g_cpus[i].started;
            uint64_t timeout = 100000000;
            while (!*flag && timeout > 0) {
                asm volatile("pause");
                timeout--;
            }

            if (!*flag) {
                KernelLogStream(ERROR, "SMP") << "AP " << i << " (LAPIC "
                    << (uint64_t)g_cpus[i].lapicId << ") failed to start";
            }
        }

        // Count how many actually started
        int onlineCount = 1;  // BSP
        for (int i = 1; i < g_cpuCount; i++) {
            if (g_cpus[i].started) onlineCount++;
        }

        KernelLogStream(OK, "SMP") << onlineCount << " CPU(s) online";
    }
}
