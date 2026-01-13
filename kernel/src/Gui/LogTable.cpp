/*
    * LogTable.cpp
    * Object-oriented API for building and submitting log tables
    * Copyright (c) 2025 Daniel Hammer
*/

#include "LogTable.hpp"
#include "LogWindow.hpp"
#include <Memory/Heap.hpp>

namespace Gui {

    LogTable::LogTable() : m_colCount(0) {
    }

    LogTable::~LogTable() {
        // Manually free vector arrays since kcp::vector is primitive and doesn't have a destructor
        if (m_headers.get_array()) {
            Memory::g_heap->Free(m_headers.get_array()); 
        }
        if (m_cells.get_array()) {
            Memory::g_heap->Free(m_cells.get_array());
        }
    }

    void LogTable::AddColumn(const char* name) {
        m_headers.push_back(name);
        m_colCount++;
    }

    void LogTable::AddRow(std::initializer_list<const char*> values) {
        for (auto val : values) {
            m_cells.push_back(val);
        }
        
        // Pad with empty strings if row provided fewer values than columns
        int itemsInRow = values.size();
        if (itemsInRow < m_colCount) {
             for (int i = 0; i < (m_colCount - itemsInRow); i++) {
                 m_cells.push_back("");
             }
        }
    }

    void LogTable::Submit() {
        if (!g_logWindow) return;

        // Submit Header
        if (m_headers.size() > 0) {
            g_logWindow->AddTableRow(true, m_colCount, m_headers.get_array());
        }

        // Submit Rows
        // We need to construct a temporary array of pointers for each row to pass to AddTableRow
        // which expects const char** or variable args
        // Since m_cells is flat, we iterate in chunks of m_colCount
        
        int totalCells = m_cells.size();
        int rows = totalCells / m_colCount;

        for (int r = 0; r < rows; r++) {
            // Point directly into the cells array
            const char** rowPtr = &m_cells.get_array()[r * m_colCount];
            g_logWindow->AddTableRow(false, m_colCount, rowPtr);
        }
    }

}
