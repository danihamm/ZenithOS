/*
    * AcpiShutdown.cpp
    * ACPI S5 (soft-off) shutdown via PM1 control registers
    * Copyright (c) 2026 Daniel Hammer
*/

#include "AcpiShutdown.hpp"
#include "AcpiSleep.hpp"
#include "AcpiEvents.hpp"
#include <ACPI/FADT.hpp>
#include <ACPI/AML/AmlParser.hpp>
#include <ACPI/AML/AmlInterpreter.hpp>
#include <ACPI/AcpiDevices.hpp>
#include <Io/IoPort.hpp>
#include <Terminal/Terminal.hpp>
#include <CppLib/Stream.hpp>
#include <Memory/HHDM.hpp>

using namespace Kt;

namespace Hal {
    namespace AcpiShutdown {

        static constexpr uint16_t SLP_EN      = 1 << 13;
        static constexpr uint16_t SLP_TYP_MASK = 0x1C00; // bits 10-12

        static bool     g_available = false;
        static uint32_t g_pm1aControlBlock = 0;
        static uint32_t g_pm1bControlBlock = 0;
        static uint16_t g_slpTypA = 0;
        static uint16_t g_slpTypB = 0;

        void Initialize(ACPI::CommonSDTHeader* xsdt) {
            FADT::ParsedFADT fadt{};
            if (!FADT::Parse(xsdt, fadt) || !fadt.Valid) {
                KernelLogStream(ERROR, "ACPI") << "Failed to parse FADT - ACPI shutdown unavailable";
                return;
            }

            // Map and parse the DSDT to find \_S5_
            auto* dsdt = (void*)Memory::HHDM(fadt.DsdtAddress);
            AML::S5Object s5 = AML::FindS5(dsdt);

            if (!s5.Valid) {
                KernelLogStream(ERROR, "ACPI") << "Could not find \\_S5_ in DSDT - ACPI shutdown unavailable";
                return;
            }

            g_pm1aControlBlock = fadt.PM1aControlBlock;
            g_pm1bControlBlock = fadt.PM1bControlBlock;
            g_slpTypA = s5.SLP_TYPa;
            g_slpTypB = s5.SLP_TYPb;
            g_available = true;

            KernelLogStream(OK, "ACPI") << "ACPI shutdown initialized (PM1a=" << base::hex
                << (uint64_t)g_pm1aControlBlock << " SLP_TYPa=" << (uint64_t)g_slpTypA << ")";

            // Initialize the full AML interpreter with the DSDT.
            // This loads the entire DSDT namespace, enabling device enumeration,
            // method evaluation, and field I/O for the rest of the kernel.
            AML::InitializeInterpreter(dsdt);

            // Enumerate ACPI devices now that the namespace is loaded
            if (AML::GetInterpreter().IsInitialized()) {
                AcpiDevices::DeviceList devices{};
                AcpiDevices::EnumerateAll(devices);
            }

            // Initialize S3 suspend support (reads FACS and \_S3_ from namespace)
            AcpiSleep::Initialize(xsdt);

            // Note: ACPI event handling (SCI, power button) is initialized
            // from Main.cpp after the APIC subsystem is ready.
        }

        bool IsAvailable() {
            return g_available;
        }

        void Shutdown() {
            if (!g_available) return;

            KernelLogStream(INFO, "ACPI") << "Performing ACPI S5 shutdown...";

            // Evaluate _PTS(5) — Prepare To Sleep (S5 = soft-off)
            auto& interp = AML::GetInterpreter();
            if (interp.IsInitialized()) {
                int32_t node = interp.GetNamespace().FindNode("\\_PTS");
                if (node >= 0) {
                    AML::Object arg{};
                    arg.Type = AML::ObjectType::Integer;
                    arg.Integer = 5;
                    AML::Object result{};
                    interp.EvaluateMethod("\\_PTS", &arg, 1, result);
                }
            }

            asm volatile("cli");

            // Phase 1: Write SLP_TYP to PM1x_CNT without SLP_EN,
            //          preserving existing register bits
            uint16_t pm1a = Io::In16((uint16_t)g_pm1aControlBlock);
            pm1a = (pm1a & ~SLP_TYP_MASK) | (g_slpTypA << 10);
            Io::Out16(pm1a, (uint16_t)g_pm1aControlBlock);

            if (g_pm1bControlBlock != 0) {
                uint16_t pm1b = Io::In16((uint16_t)g_pm1bControlBlock);
                pm1b = (pm1b & ~SLP_TYP_MASK) | (g_slpTypB << 10);
                Io::Out16(pm1b, (uint16_t)g_pm1bControlBlock);
            }

            // Flush all caches before entering sleep state
            asm volatile("wbinvd");

            // Phase 2: Assert SLP_EN to trigger the transition
            Io::Out16(pm1a | SLP_EN, (uint16_t)g_pm1aControlBlock);

            if (g_pm1bControlBlock != 0) {
                uint16_t pm1b = Io::In16((uint16_t)g_pm1bControlBlock);
                Io::Out16(pm1b | SLP_EN, (uint16_t)g_pm1bControlBlock);
            }

            // Halt and wait for the hardware to power off
            for (;;) {
                asm volatile("hlt");
            }
        }
    };
};
