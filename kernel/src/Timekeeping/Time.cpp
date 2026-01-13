/*
    * Time.cpp
    * Copyright (c) 2025 Daniel Hammer
*/

#include "Time.hpp"
#include <Gui/DebugGui.hpp>

void Timekeeping::Init(uint16_t Year, uint8_t Month, uint8_t Day, uint8_t Hour, uint8_t Minute, uint8_t Second) {
    /* Hardcode CET for now */
    TimeZone CET = {
        "Central European Time",
        "CET",
        1, /* UTC+1 */
        0,
        false
    };

    Gui::Log(Gui::LogLevel::Info, "Time", "Setting timezone to CET (UTC+1)");

    // Apply timezone offset
    Minute = Minute + CET.MinuteOffset;
    Hour = Hour + CET.HourOffset;
    if (Minute >= 60) {
        Minute -= 60;
        Hour += 1;
    }
    if (Hour >= 24) {
        Hour -= 24;
        Day += 1;
        /* Note: No month/day overflow handling yet */
    }

    // Update GUI time display
    Gui::UpdateTime(Year, Month, Day, Hour, Minute, Second);

    Gui::Log(Gui::LogLevel::Ok, "Time", "System time initialized");
}
