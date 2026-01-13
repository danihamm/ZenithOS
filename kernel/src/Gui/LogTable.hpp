#pragma once

#include <CppLib/Vector.hpp>
#include <initializer_list>

namespace Gui {

    class LogTable {
    public:
        LogTable();
        ~LogTable();

        // Add a column definition
        void AddColumn(const char* name);

        // Add a row of values
        void AddRow(std::initializer_list<const char*> values);

        // Submit the table to the global log window
        void Submit();

    private:
        kcp::vector<const char*> m_headers;
        kcp::vector<const char*> m_cells;
        int m_colCount;
    };

}
