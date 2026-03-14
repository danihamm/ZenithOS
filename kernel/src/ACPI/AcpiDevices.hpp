/*
    * AcpiDevices.hpp
    * ACPI device enumeration — walks namespace to discover and query devices
    * Copyright (c) 2026 Daniel Hammer
*/

#pragma once
#include <cstdint>
#include <ACPI/AML/AmlResource.hpp>

namespace Hal {
    namespace AcpiDevices {

        // ── Device Status Flags (_STA) ──────────────────────────────────
        static constexpr uint32_t STA_PRESENT     = (1 << 0);
        static constexpr uint32_t STA_ENABLED     = (1 << 1);
        static constexpr uint32_t STA_VISIBLE     = (1 << 2);
        static constexpr uint32_t STA_FUNCTIONAL  = (1 << 3);
        static constexpr uint32_t STA_BATTERY     = (1 << 4); // battery present (for batteries)

        // Default _STA when no _STA method exists: present + enabled + visible + functional
        static constexpr uint32_t STA_DEFAULT     = 0x0F;

        // ── Device Info ─────────────────────────────────────────────────
        struct DeviceInfo {
            char     Path[128];       // full namespace path
            char     HardwareId[16];  // _HID value (e.g. "PNP0A03", "ACPI0001")
            char     UniqueId[16];    // _UID value
            uint64_t Address;         // _ADR value (for PCI: (device << 16) | function)
            uint32_t Status;          // _STA flags
            int32_t  NodeIndex;       // namespace node index
            bool     IsPresent;       // STA_PRESENT && STA_FUNCTIONAL
        };

        static constexpr int MaxDevices = 64;

        struct DeviceList {
            DeviceInfo Devices[MaxDevices];
            int        Count;

            DeviceList() : Count(0) {}

            // Find a device by HID. Returns nullptr if not found.
            const DeviceInfo* FindByHid(const char* hid) const;

            // Find a device by path. Returns nullptr if not found.
            const DeviceInfo* FindByPath(const char* path) const;
        };

        // ── Enumeration API ─────────────────────────────────────────────

        // Enumerate all ACPI devices in the namespace.
        // The interpreter must be initialized first (LoadTable called).
        void EnumerateAll(DeviceList& result);

        // Evaluate _STA for a device node. Returns STA_DEFAULT if no _STA exists.
        uint32_t EvaluateSta(int32_t deviceNodeIndex);

        // Evaluate _ADR for a device node. Returns 0 if no _ADR exists.
        uint64_t EvaluateAdr(int32_t deviceNodeIndex);

        // Evaluate _HID for a device node. Returns false if no _HID exists.
        // Writes the HID string (or EISAID) into outHid.
        bool EvaluateHid(int32_t deviceNodeIndex, char* outHid, int maxLen);

        // Evaluate _UID for a device node.
        bool EvaluateUid(int32_t deviceNodeIndex, char* outUid, int maxLen);

        // Evaluate _CRS (Current Resource Settings) for a device node.
        bool EvaluateCrs(int32_t deviceNodeIndex, AML::ResourceList& result);

        // ── Well-known HIDs ─────────────────────────────────────────────
        // PCI Host Bridge
        static constexpr const char* HID_PCI_HOST   = "PNP0A03";
        static constexpr const char* HID_PCIE_HOST  = "PNP0A08";
        // System Timer (PIT)
        static constexpr const char* HID_PIT        = "PNP0100";
        // RTC / CMOS
        static constexpr const char* HID_RTC        = "PNP0B00";
        // Keyboard controller (i8042)
        static constexpr const char* HID_KBD        = "PNP0303";
        // PS/2 Mouse
        static constexpr const char* HID_MOUSE      = "PNP0F13";
        // Serial port (UART)
        static constexpr const char* HID_SERIAL     = "PNP0501";
        // ACPI Embedded Controller
        static constexpr const char* HID_EC         = "PNP0C09";
        // ACPI Power Button
        static constexpr const char* HID_PWRBTN     = "PNP0C0C";
        // ACPI Sleep Button
        static constexpr const char* HID_SLPBTN     = "PNP0C0E";
        // ACPI Lid device
        static constexpr const char* HID_LID        = "PNP0C0D";
        // ACPI Thermal Zone
        static constexpr const char* HID_THERMAL    = "PNP0C0B";
        // ACPI Battery
        static constexpr const char* HID_BATTERY    = "PNP0C0A";
        // ACPI AC Adapter
        static constexpr const char* HID_AC_ADAPTER = "ACPI0003";

        // ── Sleep State Support ─────────────────────────────────────────
        // Extract sleep state values from namespace objects (\_S0_ through \_S5_).
        struct SleepState {
            uint16_t SLP_TYPa;
            uint16_t SLP_TYPb;
            bool     Valid;
        };

        // Get the sleep type values for a given sleep state (0-5).
        SleepState GetSleepState(int state);
    };
};
