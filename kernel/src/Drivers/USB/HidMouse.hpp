/*
    * HidMouse.hpp
    * USB HID Mouse driver (Report Protocol with descriptor parsing)
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <cstdint>

namespace Drivers::USB::HidMouse {

    // Parsed layout of a mouse HID report
    struct MouseReportFormat {
        bool     hasReportId;
        uint8_t  reportId;
        uint8_t  buttonBitOffset;  // bit offset of first button
        uint8_t  buttonCount;
        uint8_t  xBitOffset;
        uint8_t  xBitSize;        // 8 or 16 typically
        uint8_t  yBitOffset;
        uint8_t  yBitSize;
        uint8_t  scrollBitOffset;
        uint8_t  scrollBitSize;   // 0 = no scroll wheel
    };

    // Register a mouse device by slot ID
    void RegisterDevice(uint8_t slotId);

    // Parse a HID Report Descriptor to determine the report layout.
    // Must be called before ProcessReport for Report Protocol mice.
    void ParseReportDescriptor(const uint8_t* desc, uint16_t length);

    // Process an incoming mouse report using the parsed format
    void ProcessReport(const uint8_t* data, uint16_t length);

};
