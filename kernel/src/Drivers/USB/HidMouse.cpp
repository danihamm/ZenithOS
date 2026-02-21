/*
    * HidMouse.cpp
    * USB HID Mouse driver (Report Protocol with descriptor parsing)
    * Copyright (c) 2025 Daniel Hammer
*/

#include "HidMouse.hpp"
#include <Drivers/PS2/Mouse.hpp>
#include <Terminal/Terminal.hpp>
#include <CppLib/Stream.hpp>

using namespace Kt;

namespace Drivers::USB::HidMouse {

    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------

    static uint8_t g_SlotId = 0;
    static MouseReportFormat g_Format = {};
    static bool g_FormatValid = false;

    // -------------------------------------------------------------------------
    // HID Report Descriptor parsing
    // -------------------------------------------------------------------------

    // HID item size lookup: bSize field (bits 0-1) → byte count
    static uint8_t ItemDataSize(uint8_t bSize) {
        static const uint8_t sizes[] = {0, 1, 2, 4};
        return sizes[bSize & 0x03];
    }

    // Read an unsigned value from 1-4 bytes (little-endian)
    static uint32_t ReadItemData(const uint8_t* p, uint8_t size) {
        switch (size) {
        case 1: return p[0];
        case 2: return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
        case 4: return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
                     | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        default: return 0;
        }
    }

    // HID usage page constants
    static constexpr uint16_t USAGE_PAGE_GENERIC_DESKTOP = 0x01;
    static constexpr uint16_t USAGE_PAGE_BUTTON          = 0x09;

    // HID usage constants (Generic Desktop)
    static constexpr uint16_t USAGE_X      = 0x30;
    static constexpr uint16_t USAGE_Y      = 0x31;
    static constexpr uint16_t USAGE_WHEEL  = 0x38;

    // Maximum local usages we track between Input items
    static constexpr int MAX_USAGES = 16;

    void ParseReportDescriptor(const uint8_t* desc, uint16_t length) {
        g_FormatValid = false;

        MouseReportFormat fmt = {};

        // Global state
        uint16_t usagePage  = 0;
        uint32_t reportSize = 0;  // bits per field
        uint32_t reportCount = 0; // number of fields

        // Local state (reset after each Main item)
        uint16_t usages[MAX_USAGES] = {};
        int usageCount = 0;
        bool hasUsageRange = false;

        // Running bit offset within the report (excluding report ID byte)
        uint16_t bitOffset = 0;

        uint16_t pos = 0;
        while (pos < length) {
            uint8_t header = desc[pos];

            // Long items (0xFE prefix) — skip
            if (header == 0xFE) {
                if (pos + 1 >= length) break;
                uint8_t dataSize = desc[pos + 1];
                pos += 3 + dataSize;
                continue;
            }

            uint8_t bSize = header & 0x03;
            uint8_t bType = (header >> 2) & 0x03;
            uint8_t bTag  = (header >> 4) & 0x0F;
            uint8_t dataSize = ItemDataSize(bSize);

            if (pos + 1 + dataSize > length) break;
            uint32_t data = ReadItemData(&desc[pos + 1], dataSize);
            pos += 1 + dataSize;

            if (bType == 1) {
                // Global items
                switch (bTag) {
                case 0: usagePage   = (uint16_t)data; break;  // Usage Page
                case 7: reportSize  = data; break;             // Report Size
                case 8: fmt.hasReportId = true;                // Report ID
                        fmt.reportId = (uint8_t)data; break;
                case 9: reportCount = data; break;             // Report Count
                }
            } else if (bType == 2) {
                // Local items
                switch (bTag) {
                case 0: // Usage
                    if (usageCount < MAX_USAGES)
                        usages[usageCount++] = (uint16_t)data;
                    break;
                case 1: hasUsageRange = true; break; // Usage Minimum
                case 2: break; // Usage Maximum
                }
            } else if (bType == 0) {
                // Main items
                if (bTag == 8 || bTag == 9) {
                    // Input (tag 8) or Output (tag 9)
                    bool isConstant = (data & 0x01);

                    if (bTag == 8 && !isConstant) {
                        // Data input fields — map usages to bit offsets
                        if (usagePage == USAGE_PAGE_BUTTON && hasUsageRange) {
                            fmt.buttonBitOffset = bitOffset;
                            fmt.buttonCount = (uint8_t)reportCount;
                        } else if (usagePage == USAGE_PAGE_GENERIC_DESKTOP) {
                            for (uint32_t i = 0; i < reportCount && i < (uint32_t)usageCount; i++) {
                                uint16_t u = usages[i];
                                uint16_t off = bitOffset + (uint16_t)(i * reportSize);
                                if (u == USAGE_X) {
                                    fmt.xBitOffset = off;
                                    fmt.xBitSize = (uint8_t)reportSize;
                                } else if (u == USAGE_Y) {
                                    fmt.yBitOffset = off;
                                    fmt.yBitSize = (uint8_t)reportSize;
                                } else if (u == USAGE_WHEEL) {
                                    fmt.scrollBitOffset = off;
                                    fmt.scrollBitSize = (uint8_t)reportSize;
                                }
                            }
                        }
                    }

                    // Advance bit offset for all input fields (data and constant/padding)
                    bitOffset += (uint16_t)(reportSize * reportCount);
                }

                // Reset local state after any Main item
                usageCount = 0;
                hasUsageRange = false;
            }
        }

        // Validate: we need at least X and Y
        if (fmt.xBitSize > 0 && fmt.yBitSize > 0) {
            g_Format = fmt;
            g_FormatValid = true;

            KernelLogStream(INFO, "USB/Mouse")
                << "Report format: buttons=" << (uint64_t)fmt.buttonCount
                << " X@" << (uint64_t)fmt.xBitOffset << ":" << (uint64_t)fmt.xBitSize
                << " Y@" << (uint64_t)fmt.yBitOffset << ":" << (uint64_t)fmt.yBitSize
                << " scroll=" << (uint64_t)fmt.scrollBitSize
                << (fmt.hasReportId ? " (has report ID)" : "");
        } else {
            KernelLogStream(WARNING, "USB/Mouse") << "Could not parse report descriptor, using boot protocol fallback";
        }
    }

    // -------------------------------------------------------------------------
    // Bit-field extraction
    // -------------------------------------------------------------------------

    static int32_t ExtractSigned(const uint8_t* data, uint16_t bitOffset, uint8_t bitSize) {
        int32_t value = 0;
        for (uint8_t i = 0; i < bitSize; i++) {
            uint16_t byteIdx = (bitOffset + i) / 8;
            uint8_t  bitIdx  = (bitOffset + i) % 8;
            if (data[byteIdx] & (1 << bitIdx))
                value |= (1 << i);
        }
        // Sign extend
        if (bitSize < 32 && (value & (1 << (bitSize - 1)))) {
            value |= ~((1 << bitSize) - 1);
        }
        return value;
    }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    void RegisterDevice(uint8_t slotId) {
        g_SlotId = slotId;
        KernelLogStream(OK, "USB/Mouse") << "Registered HID mouse on slot " << (uint64_t)slotId;
    }

    void ProcessReport(const uint8_t* data, uint16_t length) {
        if (length < 3) return;

        if (!g_FormatValid) {
            // Fallback: assume boot protocol format
            uint8_t buttons = data[0] & 0x07;
            int8_t deltaX   = (int8_t)data[1];
            int8_t deltaY   = (int8_t)data[2];
            int8_t scroll   = (length >= 4) ? (int8_t)data[3] : 0;
            Drivers::PS2::Mouse::InjectMouseReport(buttons, deltaX, deltaY, scroll);
            return;
        }

        // Skip report ID byte if present
        uint16_t byteOffset = g_Format.hasReportId ? 8 : 0;

        // Extract buttons
        uint8_t buttons = 0;
        for (uint8_t i = 0; i < g_Format.buttonCount && i < 8; i++) {
            uint16_t bit = byteOffset + g_Format.buttonBitOffset + i;
            if (data[bit / 8] & (1 << (bit % 8)))
                buttons |= (1 << i);
        }

        // Extract X, Y
        int32_t rawX = ExtractSigned(data, byteOffset + g_Format.xBitOffset, g_Format.xBitSize);
        int32_t rawY = ExtractSigned(data, byteOffset + g_Format.yBitOffset, g_Format.yBitSize);

        // Clamp to int8_t range for InjectMouseReport
        if (rawX > 127) rawX = 127;
        if (rawX < -128) rawX = -128;
        if (rawY > 127) rawY = 127;
        if (rawY < -128) rawY = -128;

        // Extract scroll wheel
        int8_t scroll = 0;
        if (g_Format.scrollBitSize > 0) {
            int32_t rawScroll = ExtractSigned(data, byteOffset + g_Format.scrollBitOffset, g_Format.scrollBitSize);
            if (rawScroll > 127) rawScroll = 127;
            if (rawScroll < -128) rawScroll = -128;
            scroll = (int8_t)rawScroll;
        }

        Drivers::PS2::Mouse::InjectMouseReport(buttons, (int8_t)rawX, (int8_t)rawY, scroll);
    }

}
