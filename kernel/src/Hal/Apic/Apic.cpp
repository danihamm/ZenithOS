/*
    * Apic.cpp
    * Local APIC (Advanced Programmable Interrupt Controller)
    * Copyright (c) 2025 Daniel Hammer
*/

#include "Apic.hpp"
#include <Terminal/Terminal.hpp>
#include <CppLib/Stream.hpp>
#include <Memory/HHDM.hpp>

using namespace Kt;

namespace Hal {
    namespace LocalApic {
        static volatile uint32_t* g_apicBase = nullptr;

        static inline uint64_t ReadMSR(uint32_t msr) {
            uint32_t lo, hi;
            asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
            return ((uint64_t)hi << 32) | lo;
        }

        static inline void WriteMSR(uint32_t msr, uint64_t value) {
            uint32_t lo = (uint32_t)(value & 0xFFFFFFFF);
            uint32_t hi = (uint32_t)(value >> 32);
            asm volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
        }

        uint32_t ReadRegister(uint32_t reg) {
            return g_apicBase[reg / 4];
        }

        void WriteRegister(uint32_t reg, uint32_t value) {
            g_apicBase[reg / 4] = value;
        }

        void Initialize(uint64_t apicBasePhys) {
            // Read the APIC base MSR to verify/confirm the base address
            uint64_t msrValue = ReadMSR(MSR_APIC_BASE);
            uint64_t msrBase = msrValue & 0xFFFFF000;

            KernelLogStream(DEBUG, "APIC") << "MSR APIC base: " << base::hex << msrBase;
            KernelLogStream(DEBUG, "APIC") << "MADT APIC base: " << base::hex << apicBasePhys;

            // Use the MADT-provided address (it should match MSR)
            g_apicBase = (volatile uint32_t*)Memory::HHDM(apicBasePhys);

            // Enable the APIC by setting bit 8 (APIC Software Enable) in SVR
            // and set the spurious interrupt vector
            uint32_t svr = ReadRegister(REG_SPURIOUS);
            svr |= (1 << 8);           // Enable APIC
            svr = (svr & 0xFFFFFF00) | SPURIOUS_VECTOR;  // Set spurious vector
            WriteRegister(REG_SPURIOUS, svr);

            // Set Task Priority to 0 to accept all interrupts
            WriteRegister(REG_TPR, 0);

            uint32_t version = ReadRegister(REG_VERSION);
            uint32_t id = GetId();

            KernelLogStream(OK, "APIC") << "Local APIC initialized: id=" << base::dec << (uint64_t)id
                << " version=" << base::hex << (uint64_t)(version & 0xFF)
                << " max LVT=" << base::dec << (uint64_t)((version >> 16) & 0xFF);
        }

        void InitializeAP() {
            if (!g_apicBase) return;

            // Re-assert APIC Global Enable in MSR
            uint64_t msrValue = ReadMSR(MSR_APIC_BASE);
            if (!(msrValue & (1ULL << 11))) {
                msrValue |= (1ULL << 11);
                WriteMSR(MSR_APIC_BASE, msrValue);
            }

            // Enable APIC software enable + set spurious vector
            uint32_t svr = ReadRegister(REG_SPURIOUS);
            svr |= (1 << 8);
            svr = (svr & 0xFFFFFF00) | SPURIOUS_VECTOR;
            WriteRegister(REG_SPURIOUS, svr);

            // Accept all interrupts
            WriteRegister(REG_TPR, 0);
            // No log here -- APs boot in parallel and the BSP logs a summary.
        }

        void Reinitialize() {
            if (!g_apicBase) return;

            // Re-assert the APIC Global Enable in the IA32_APIC_BASE MSR.
            // Some firmware clears bit 11 during S3 resume. If the global
            // enable is off, all subsequent MMIO register writes (SVR, TPR,
            // timer LVT, etc.) are silently ignored - meaning no interrupts
            // are delivered, no timer fires, and the system appears frozen.
            uint64_t msrValue = ReadMSR(MSR_APIC_BASE);
            if (!(msrValue & (1ULL << 11))) {
                msrValue |= (1ULL << 11);
                WriteMSR(MSR_APIC_BASE, msrValue);
            }

            // Re-enable APIC software enable bit in SVR + set spurious vector
            uint32_t svr = ReadRegister(REG_SPURIOUS);
            svr |= (1 << 8);
            svr = (svr & 0xFFFFFF00) | SPURIOUS_VECTOR;
            WriteRegister(REG_SPURIOUS, svr);

            // Accept all interrupts
            WriteRegister(REG_TPR, 0);
        }

        void SendEOI() {
            WriteRegister(REG_EOI, 0);
        }

        uint32_t GetId() {
            return (ReadRegister(REG_ID) >> 24) & 0xFF;
        }
    };
};
