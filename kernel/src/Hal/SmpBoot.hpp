/*
    * SmpBoot.hpp
    * Symmetric Multiprocessing bootstrap and per-CPU data
    * Copyright (c) 2026 Daniel Hammer
*/

#pragma once
#include <cstdint>
#include <Hal/GDT.hpp>

// ====================================================================
// Assembly-visible offsets into CpuData
// These MUST match the struct layout below. Verified by static_assert
// in SmpBoot.cpp.
// ====================================================================
#define CPUDATA_SELF_PTR       0
#define CPUDATA_KERNEL_RSP     8
#define CPUDATA_USER_RSP       16
#define CPUDATA_CURRENT_SLOT   24

namespace Smp {

    static constexpr int MaxCPUs = 64;

    struct CpuData {
        // === Fields accessed by assembly (offsets defined above) ===
        uint64_t selfPtr;          // offset 0:  pointer to self
        uint64_t kernelRsp;        // offset 8:  kernel stack top for SYSCALL
        uint64_t userRspScratch;   // offset 16: scratch for saving user RSP
        int32_t  currentSlot;      // offset 24: process table slot, -1 = idle
        int32_t  _pad0;            // offset 28: padding

        // === C++-only fields ===
        int cpuIndex;              // 0-based CPU index
        uint32_t lapicId;          // local APIC ID
        uint64_t idleSavedRsp;     // RSP saved when switching from idle to process
        volatile bool started;     // set by AP after init is complete

        Hal::TSS64* tss;          // pointer to this CPU's TSS

        // Per-CPU GDT and TSS (APs use these; BSP uses globals)
        Hal::BasicGDT cpuGdt __attribute__((aligned(16)));
        Hal::TSS64    cpuTss __attribute__((aligned(16)));
    };

    // Get the current CPU's data (reads gs:0 which holds the self-pointer)
    inline CpuData* GetCurrentCpuData() {
        CpuData* ptr;
        asm volatile("mov %%gs:0, %0" : "=r"(ptr));
        return ptr;
    }

    // Get CPU data by index
    CpuData* GetCpuData(int index);

    // Number of online CPUs
    int GetCpuCount();

    // Initialize BSP per-CPU data (call before interrupts are enabled)
    void InitBsp();

    // Boot all Application Processors (call after all subsystems ready)
    void BootAPs();
}
