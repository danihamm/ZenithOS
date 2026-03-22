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
};