/*
    * LogWindow.cpp
    * Scrolling log display with circular buffer
    * Copyright (c) 2025 Daniel Hammer
*/

#include "LogWindow.hpp"
#include "Graphics.hpp"
#include "Color.hpp"
#include "Font.hpp"

namespace Gui {

    // Global instance pointer
    LogWindow* g_logWindow = nullptr;

    LogWindow::LogWindow(int x, int y, int w, int h)
        : Panel(x, y, w, h, "Kernel Log", true)
        , m_head(0)
        , m_count(0)
        , m_lastRenderedCount(0)
    {
        // Initialize all entries as unused
        for (int i = 0; i < MAX_LINES; ++i) {
            m_lines[i].used = false;
            m_lines[i].text[0] = '\0';
            m_lines[i].component[0] = '\0';
        }
    }

    void LogWindow::CopyString(char* dest, const char* src, int maxLen) {
        int i = 0;
        while (src[i] != '\0' && i < maxLen - 1) {
            dest[i] = src[i];
            ++i;
        }
        dest[i] = '\0';
    }

    uint32_t LogWindow::GetLevelColor(LogLevel level) const {
        switch (level) {
            case LogLevel::Info:    return Colors::Blue;
            case LogLevel::Warning: return Colors::Yellow;
            case LogLevel::Error:   return Colors::Red;
            case LogLevel::Debug:   return Colors::Magenta;
            case LogLevel::Ok:      return Colors::Green;
            default:                return Colors::Text;
        }
    }

    const char* LogWindow::GetLevelPrefix(LogLevel level) const {
        switch (level) {
            case LogLevel::Info:    return "INFO";
            case LogLevel::Warning: return "WARN";
            case LogLevel::Error:   return "ERROR";
            case LogLevel::Debug:   return "DEBUG";
            case LogLevel::Ok:      return "OK";
            default:                return "???";
        }
    }

    void LogWindow::AddLine(const char* component, LogLevel level, const char* text) {
        LogEntry& entry = m_lines[m_head];

        CopyString(entry.component, component, MAX_COMPONENT_LENGTH);
        CopyString(entry.text, text, MAX_TEXT_LENGTH);
        entry.level = level;
        entry.type = LogEntryType::Normal;
        entry.used = true;

        // Advance head (circular buffer)
        m_head = (m_head + 1) % MAX_LINES;
        if (m_count < MAX_LINES) {
            ++m_count;
        }

        MarkDirty();
    }

    void LogWindow::AddTableRow(bool isHeader, int count, const char** columns) {
        LogEntry& entry = m_lines[m_head];
        
        // Use component field to store "TABLE" for identification
        CopyString(entry.component, "TABLE", MAX_COMPONENT_LENGTH);
        entry.level = LogLevel::Info;
        entry.type = isHeader ? LogEntryType::TableHeader : LogEntryType::TableRow;
        entry.used = true;

        // Format columns into text separated by special char
        int offset = 0;
        for (int i = 0; i < count; ++i) {
            const char* str = columns[i];
            int j = 0;
            while (str[j] != '\0' && offset < MAX_TEXT_LENGTH - 2) {
                entry.text[offset++] = str[j++];
            }
            if (i < count - 1) {
                entry.text[offset++] = COLUMN_SEPARATOR;
            }
        }
        entry.text[offset] = '\0';

        // Advance head
        m_head = (m_head + 1) % MAX_LINES;
        if (m_count < MAX_LINES) {
            ++m_count;
        }

        MarkDirty();
    }

    void LogWindow::Clear() {
        for (int i = 0; i < MAX_LINES; ++i) {
            m_lines[i].used = false;
        }
        m_head = 0;
        m_count = 0;
        m_lastRenderedCount = 0;
        MarkDirty();
    }

    int LogWindow::GetVisibleLineCount() const {
        // Account for frame and title
        int contentHeight = GetContentHeight() - FONT_HEIGHT - 4;  // Title takes one line
        return contentHeight / (FONT_HEIGHT + 2);  // 2px line spacing
    }

    void LogWindow::Render() {
        // Draw frame
        DrawFrame();

        // Fill content area
        int contentX = GetContentX();
        int contentY = GetContentY();
        int contentW = GetContentWidth();
        int contentH = GetContentHeight();

        FillRect(contentX, contentY, contentW, contentH, Colors::Surface);

        // Draw title bar area
        int titleBarHeight = FONT_HEIGHT + 8;
        FillRect(contentX, contentY, contentW, titleBarHeight, Colors::Overlay);
        DrawString(contentX + 4, contentY + 4, "Kernel Log", Colors::Text, Colors::Overlay);
        DrawHLine(contentX, contentY + titleBarHeight, contentW, Colors::Border);

        // Calculate visible area for log entries
        int logY = contentY + titleBarHeight + 4;
        int logHeight = contentH - titleBarHeight - 8;
        int visibleLines = logHeight / (FONT_HEIGHT + 2);

        if (m_count == 0) {
            DrawString(contentX + 4, logY, "No log entries", Colors::TextDim, Colors::Surface);
            return;
        }

        // Calculate starting position for auto-scroll (show most recent entries)
        int startIdx;
        int entriesToShow = (m_count < visibleLines) ? m_count : visibleLines;

        if (m_count <= MAX_LINES) {
            // Not yet wrapped
            startIdx = (m_count > visibleLines) ? (m_count - visibleLines) : 0;
        } else {
            // Buffer has wrapped - start from oldest visible entry
            startIdx = (m_head + MAX_LINES - visibleLines) % MAX_LINES;
        }

        // Render visible log entries
        int y = logY;
        for (int i = 0; i < entriesToShow; ++i) {
            int idx;
            if (m_count <= MAX_LINES) {
                idx = startIdx + i;
            } else {
                idx = (startIdx + i) % MAX_LINES;
            }

            const LogEntry& entry = m_lines[idx];
            if (!entry.used) continue;

            DrawEntry(entry, y, contentX, contentW);

            y += FONT_HEIGHT + 4; // Spacing for row height
        }

        m_lastRenderedCount = m_count;
    }

    void LogWindow::DrawEntry(const LogEntry& entry, int y, int contentX, int contentW) {
        int x = contentX + 4;

        if (entry.type == LogEntryType::Normal) {
            // Draw component name
            DrawString(x, y, entry.component, Colors::Cyan, Colors::Surface);
            x += 12 * FONT_WIDTH;  // Fixed width for component

            // Draw separator
            DrawString(x, y, ": [", Colors::TextDim, Colors::Surface);
            x += 3 * FONT_WIDTH;

            // Draw level with color
            uint32_t levelColor = GetLevelColor(entry.level);
            const char* levelPrefix = GetLevelPrefix(entry.level);
            DrawString(x, y, levelPrefix, levelColor, Colors::Surface);
            x += 5 * FONT_WIDTH;  // Fixed width for level

            // Draw closing bracket and message
            DrawString(x, y, "] ", Colors::TextDim, Colors::Surface);
            x += 2 * FONT_WIDTH;

            // Draw message text
            DrawString(x, y, entry.text, Colors::Text, Colors::Surface);
        } else {
            // Render Table Row
            // Determine layout
            int colCount = 1;
            for (int k = 0; entry.text[k] != '\0'; k++) {
                if (entry.text[k] == COLUMN_SEPARATOR) colCount++;
            }

            // "Inline" width calculation: Fixed width per column to keep it compact but aligned
            constexpr int MIN_COL_WIDTH = 100;
            
            // Calculate offset to match standard log entry text start
            // Component(12) + ": ["(3) + Level(5) + "] "(2) = 22 chars
            int indentOffset = (12 + 3 + 5 + 2) * FONT_WIDTH; 
            
            int colWidth = MIN_COL_WIDTH;
            int tableWidth = colCount * colWidth;
            int startX = contentX + 4 + indentOffset;
            
            // Ensure we don't overflow the remaining space
            int maxTableWidth = contentW - 8 - indentOffset;
            if (tableWidth > maxTableWidth) {
                tableWidth = maxTableWidth;
                colWidth = tableWidth / colCount;
            }

            // Determine border colors - Solid white
            uint32_t borderColor = Colors::Text; 

            // Draw Table Borders
            // Top line for Header
            if (entry.type == LogEntryType::TableHeader) {
                DrawHLine(startX, y - 2, tableWidth, borderColor);
            }
            
            // Bottom line for everyone
            DrawHLine(startX, y + FONT_HEIGHT + 2, tableWidth, borderColor);

            // Draw columns
            int currentX = startX;
            int charIdx = 0;
            
            // Draw Left vertical line
            DrawVLine(currentX, y - 2, FONT_HEIGHT + 5, borderColor);

            for (int c = 0; c < colCount; c++) {
                char colBuffer[MAX_TEXT_LENGTH];
                int bufIdx = 0;
                
                while (entry.text[charIdx] != '\0' && entry.text[charIdx] != COLUMN_SEPARATOR) {
                    colBuffer[bufIdx++] = entry.text[charIdx++];
                }
                colBuffer[bufIdx] = '\0';
                if (entry.text[charIdx] == COLUMN_SEPARATOR) charIdx++;

                // Draw text
                DrawStringTransparent(currentX + 4, y, colBuffer, Colors::Text);

                // Draw Right vertical line for this column
                DrawVLine(currentX + colWidth, y - 2, FONT_HEIGHT + 5, borderColor);
                
                currentX += colWidth;
            }
        }
    }

    void LogWindow::PaintLastLine() {
        int contentX = GetContentX();
        int contentY = GetContentY();
        int contentW = GetContentWidth();
        int contentH = GetContentHeight();
        
        int titleBarHeight = FONT_HEIGHT + 8;
        int logY = contentY + titleBarHeight + 4;
        int logHeight = contentH - titleBarHeight - 8;
        int visibleLines = logHeight / (FONT_HEIGHT + 2);

        // Check if we need to scroll. If we do, we must fall back to full window render
        // to shift everything up. But Render() doesn't clear full screen, just the window background.
        if (m_count > visibleLines) {
            Render();
            return;
        }
        
        int rowHeight = FONT_HEIGHT + 4;
        int lastIdx = m_head - 1;
        if (lastIdx < 0) lastIdx += MAX_LINES;

        int lineY = logY + (m_count - 1) * rowHeight;
        
        const LogEntry& entry = m_lines[lastIdx];
        if (entry.used) {
            DrawEntry(entry, lineY, contentX, contentW);
        }

        m_lastRenderedCount = m_count;
    }

}
