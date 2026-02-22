/*
    * Heap.hpp
    * SYS_ALLOC, SYS_FREE syscalls
    * Copyright (c) 2026 Daniel Hammer
*/

#include <cstdint>
#include <Sched/Scheduler.hpp>
#include <Memory/Paging.hpp>
#include <Memory/HHDM.hpp>
#include <Memory/PageFrameAllocator.hpp>

namespace Zenith {
    static uint64_t Sys_Alloc(uint64_t size) {
        auto* proc = Sched::GetCurrentProcessPtr();
        if (proc == nullptr) return 0;

        // Round up to page boundary
        size = (size + 0xFFF) & ~0xFFFULL;
        if (size == 0) size = 0x1000;

        uint64_t userVa = proc->heapNext;
        uint64_t numPages = size / 0x1000;

        for (uint64_t i = 0; i < numPages; i++) {
            void* page = Memory::g_pfa->AllocateZeroed();
            if (page == nullptr) return 0;
            uint64_t physAddr = Memory::SubHHDM((uint64_t)page);
            Memory::VMM::Paging::MapUserIn(proc->pml4Phys, physAddr, userVa + i * 0x1000);
        }

        proc->heapNext += size;
        return userVa;
    }

    static void Sys_Free(uint64_t) {
        // TODO No-op for now (pages leak)
    }
};