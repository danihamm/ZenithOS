/*
    * Spinlock.cpp
    * C++ Spinlock
    * Copyright (c) 2025 Daniel Hammer
*/

#include "Spinlock.hpp"

namespace kcp {
    void Spinlock::Acquire() {
        uint64_t flags;
        asm volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
        while (atomic_flag.test_and_set(std::memory_order_acquire)) {
            asm volatile("pause");
        }
        savedFlags = flags;
    }

    void Spinlock::Release() {
        uint64_t flags = savedFlags;
        atomic_flag.clear(std::memory_order_release);
        asm volatile("push %0; popfq" :: "r"(flags) : "memory");
    }
};