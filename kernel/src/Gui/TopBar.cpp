/*
    * TopBar.cpp
    * Top bar panel with title and time display
    * Copyright (c) 2025 Daniel Hammer
*/

#include "TopBar.hpp"
#include "Graphics.hpp"
#include "Color.hpp"
#include "Font.hpp"

namespace Gui {

    // Month names for time formatting
    static const char* MonthNames[] = {
        "January", "February", "March", "April",
        "May", "June", "July", "August",
        "September", "October", "November", "December"
    };

    TopBar::TopBar(int screenWidth)
        : Panel(0, 0, screenWidth, BAR_HEIGHT, nullptr, false)
        , m_timeSet(false)
    {
        m_timeString[0] = '\0';
    }

    // Draw a simple clock icon (12x12 pixels)
    static void DrawClockIcon(int cx, int cy, uint32_t color) {
        // Draw circle outline (approximated with pixels)
        // Radius = 5 pixels, centered at (cx+6, cy+6)
        int ox = cx + 6;
        int oy = cy + 6;

        // Circle points (hand-crafted for 12x12)
        // Top and bottom
        PutPixel(ox - 1, oy - 5, color);
        PutPixel(ox,     oy - 5, color);
        PutPixel(ox + 1, oy - 5, color);
        PutPixel(ox - 1, oy + 5, color);
        PutPixel(ox,     oy + 5, color);
        PutPixel(ox + 1, oy + 5, color);

        // Left and right
        PutPixel(ox - 5, oy - 1, color);
        PutPixel(ox - 5, oy,     color);
        PutPixel(ox - 5, oy + 1, color);
        PutPixel(ox + 5, oy - 1, color);
        PutPixel(ox + 5, oy,     color);
        PutPixel(ox + 5, oy + 1, color);

        // Diagonal segments
        PutPixel(ox - 4, oy - 3, color);
        PutPixel(ox - 3, oy - 4, color);
        PutPixel(ox + 3, oy - 4, color);
        PutPixel(ox + 4, oy - 3, color);
        PutPixel(ox - 4, oy + 3, color);
        PutPixel(ox - 3, oy + 4, color);
        PutPixel(ox + 3, oy + 4, color);
        PutPixel(ox + 4, oy + 3, color);

        // Hour hand (pointing to ~10 o'clock)
        PutPixel(ox,     oy,     color);
        PutPixel(ox - 1, oy - 1, color);
        PutPixel(ox - 2, oy - 2, color);

        // Minute hand (pointing to 12 o'clock)
        PutPixel(ox, oy - 1, color);
        PutPixel(ox, oy - 2, color);
        PutPixel(ox, oy - 3, color);
    }

    void TopBar::Render() {
        // Fill background
        FillRect(m_x, m_y, m_width, m_height, Colors::TopBar);

        // Draw bottom border (separator line)
        DrawHLine(m_x, m_y + m_height - 1, m_width, Colors::Border);

        // Draw title on the left
        const char* title = "ZenithOS Debug Console";
        int textY = (m_height - FONT_HEIGHT) / 2;
        DrawString(8, textY, title, Colors::Text, Colors::TopBar);

        // Draw time on the right if set
        if (m_timeSet) {
            int timeWidth = MeasureString(m_timeString);
            int iconWidth = 14;  // 12px icon + 2px spacing
            int totalWidth = iconWidth + timeWidth;
            int startX = m_width - totalWidth - 8;

            // Draw clock icon
            int iconY = (m_height - 12) / 2;  // Center 12px icon vertically
            DrawClockIcon(startX, iconY, Colors::TextDim);

            // Draw time string
            DrawString(startX + iconWidth, textY, m_timeString, Colors::TextDim, Colors::TopBar);
        }
    }

    void TopBar::SetTime(uint16_t year, uint8_t month, uint8_t day,
                         uint8_t hour, uint8_t minute, uint8_t second) {
        // Validate month
        if (month < 1 || month > 12) month = 1;

        // Format: "13 January 2026, 14:30:45"
        // Simple integer to string conversion
        char* p = m_timeString;

        // Day
        if (day >= 10) {
            *p++ = '0' + (day / 10);
        }
        *p++ = '0' + (day % 10);
        *p++ = ' ';

        // Month name
        const char* monthName = MonthNames[month - 1];
        while (*monthName) {
            *p++ = *monthName++;
        }
        *p++ = ' ';

        // Year
        *p++ = '0' + (year / 1000);
        *p++ = '0' + ((year / 100) % 10);
        *p++ = '0' + ((year / 10) % 10);
        *p++ = '0' + (year % 10);
        *p++ = ',';
        *p++ = ' ';

        // Hour
        if (hour >= 10) {
            *p++ = '0' + (hour / 10);
        } else {
            *p++ = '0';
        }
        *p++ = '0' + (hour % 10);
        *p++ = ':';

        // Minute
        *p++ = '0' + (minute / 10);
        *p++ = '0' + (minute % 10);
        *p++ = ':';

        // Second
        *p++ = '0' + (second / 10);
        *p++ = '0' + (second % 10);

        *p = '\0';

        m_timeSet = true;
        MarkDirty();
    }

}
