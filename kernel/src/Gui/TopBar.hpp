/*
    * TopBar.hpp
    * Top bar panel with title and time display
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include "Panel.hpp"
#include <cstdint>

namespace Gui {

    class TopBar : public Panel {
        static constexpr int BAR_HEIGHT = 24;

        char m_timeString[48];
        bool m_timeSet;

    public:
        TopBar(int screenWidth);

        void Render() override;

        // Update time display
        void SetTime(uint16_t year, uint8_t month, uint8_t day,
                     uint8_t hour, uint8_t minute, uint8_t second);
    };

}
