/*
    * ApicTimer.hpp
    * Local APIC timer for periodic tick interrupts and timekeeping
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <cstdint>

namespace Timekeeping {
    // Initialize the APIC timer: calibrate against PIT, start periodic interrupts
    void ApicTimerInitialize();

    // Initialize the APIC timer on an AP (calibrate + start, no IRQ handler registration)
    void ApicTimerInitializeAP();

    // Reinitialize the APIC timer after S3 resume using the previously
    // calibrated tick rate. Skips PIT calibration and IRQ registration
    // (both survive in RAM). Only reprograms the timer hardware registers.
    void ApicTimerReinitialize();

    // Get the monotonic tick count (increments on each timer interrupt)
    uint64_t GetTicks();

    // Get elapsed milliseconds since timer initialization
    uint64_t GetMilliseconds();

    // Enable scheduler tick (called after scheduler is initialized)
    void EnableSchedulerTick();

    // Busy-wait sleep for the given number of milliseconds
    void Sleep(uint64_t ms);
};
