/*
    * Spinlock.cpp
    * C++ Spinlock
    * Copyright (c) 2025 Daniel Hammer
*/

#include "Spinlock.hpp"

namespace kcp {
    void Spinlock::Acquire() {
        while (atomic_flag.test_and_set(std::memory_order_acquire)) {
            asm volatile("pause");
        }
    }

    void Spinlock::Release() {
        atomic_flag.clear(std::memory_order_release);
    }
};