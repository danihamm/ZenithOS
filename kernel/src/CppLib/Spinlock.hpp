/*
    * Spinlock.cpp
    * C++ Spinlock
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <atomic>

namespace kcp {
    class Spinlock {
        std::atomic_flag atomic_flag{ATOMIC_FLAG_INIT};
        uint64_t savedFlags = 0;
    public:
        void Acquire();
        void Release();
    };

    // Non-interrupt-disabling mutex for subsystems that are only called
    // from process/syscall context (never from interrupt handlers).
    // Keeps interrupts enabled while spinning and while held.
    class Mutex {
        std::atomic_flag flag{ATOMIC_FLAG_INIT};
    public:
        void Acquire() {
            while (flag.test_and_set(std::memory_order_acquire)) {
                asm volatile("pause");
            }
        }
        void Release() {
            flag.clear(std::memory_order_release);
        }
    };
};