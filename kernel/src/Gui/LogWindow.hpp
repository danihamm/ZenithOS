/*
    * LogWindow.hpp
    * Scrolling log display with circular buffer
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include "Panel.hpp"
#include <cstdint>
#include <cstddef>

namespace Gui {

    enum class LogLevel {
        Info,
        Warning,
        Error,
        Debug,
        Ok
    };

    enum class LogEntryType {
        Normal,
        TableHeader,
        TableRow
    };

    class LogWindow : public Panel {
        static constexpr int MAX_LINES = 256;
        static constexpr int MAX_TEXT_LENGTH = 120;
        static constexpr int MAX_COMPONENT_LENGTH = 16;
        static constexpr char COLUMN_SEPARATOR = '\x1F'; // Unit separator

        struct LogEntry {
            char text[MAX_TEXT_LENGTH];
            char component[MAX_COMPONENT_LENGTH];
            LogLevel level;
            LogEntryType type;
            bool used;
        };

        LogEntry m_lines[MAX_LINES];
        int m_head;           // Next write position
        int m_count;          // Total entries (up to MAX_LINES)
        int m_lastRenderedCount;  // For detecting new entries

    public:
        LogWindow(int x, int y, int w, int h);

        void Render() override;

        // Add a log entry
        void AddLine(const char* component, LogLevel level, const char* text);

        // Add a table row (variable arguments for columns, NULL terminated)
        void AddTableRow(bool isHeader, int count, const char** columns);

        // Clear all log entries
        void Clear();

        // Optimized render for the last added line
        void PaintLastLine();

        // Get number of visible lines based on panel height
        int GetVisibleLineCount() const;

    private:
        // Get color for log level
        uint32_t GetLevelColor(LogLevel level) const;

        // Get level prefix string
        const char* GetLevelPrefix(LogLevel level) const;

        // Copy string with length limit
        void CopyString(char* dest, const char* src, int maxLen);

        // Helper to draw a single entry at specific Y
        void DrawEntry(const LogEntry& entry, int y, int contentX, int contentW);
    };

    // Global log window instance (created by DebugGui)
    extern LogWindow* g_logWindow;

}
