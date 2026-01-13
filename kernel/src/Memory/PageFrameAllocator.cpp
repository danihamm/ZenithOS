/*
    * PageFrameAllocator.hpp
    * Memory allocator for physical pages
    * Copyright (c) 2025 Daniel Hammer
*/

#include "PageFrameAllocator.hpp"
#include "HHDM.hpp"
#include <Libraries/Memory.hpp>
#include <Gui/DebugGui.hpp>
#include <CppLib/Spinlock.hpp>
#include <Common/Panic.hpp>

namespace Memory {
    PageFrameAllocator::PageFrameAllocator(LargestSection section) {
        /* we need the virtual address rather than the physical address, so we call the helper */
        g_section = LargestSection{
            .address = HHDM(section.address),
            .size = section.size
        };

        head.next = (Page*)g_section.address;
        head.next->size = section.size;
        head.next->next = nullptr;

        Gui::GuiLogStream(Gui::LogLevel::Debug, "PageFrameAllocator") << "New pool size: " << section.size;
    }

    void* PageFrameAllocator::Allocate() {
        Lock.Acquire();

        Page* current = head.next;
        Page* prev = &head;

        while (current != nullptr) {
            if (current->size >= 0x1000) {
                uint64_t current_addr = (uint64_t)current;
                uint64_t current_end_addr = current_addr + current->size;
                uint64_t new_size = current->size - 0x1000;

                if (new_size == 0) {
                    prev->next = current->next;

                    Lock.Release();
                    return (void*)current;
                }
                else
                {
                    current->size -= 0x1000;

                    Lock.Release();
                    return (void*)current_end_addr - 0x1000;
                }
            }

            prev = current;
            current = current->next;
        }

        Lock.Release();
        return nullptr;
    }

    void* PageFrameAllocator::AllocateZeroed() {
        auto page = Allocate();
        memset(page, 0, 0x1000);

        return page;
    }

    void* PageFrameAllocator::ReallocConsecutive(void* ptr, int n) {
        auto first = Allocate();

        for (int i = 0; i < n - 1; i++) {
            if (Allocate() == nullptr) {
                Panic("PageFrameAllocator Reallocation failed", nullptr);
            };
        }

        if (ptr != nullptr) {
            memcpy(first, ptr, n);
            Free(ptr);
        }

        return first;
    }

    void PageFrameAllocator::Free(void* ptr) {
        Lock.Acquire();
        auto prev_next = head.next;
        head.next = (Page*)ptr;

        head.next->next = prev_next;
        head.next->size = 0x1000;
        Lock.Release();
    }

    void PageFrameAllocator::Free(void* ptr, int n) {
        for (int i = 0; i < n; i++) {
            Free((void*)(uint64_t)ptr + (0x1000 * n));
        }
    }
};