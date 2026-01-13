/*
    * HeapDebugger.hpp
    * Heap status visualization panel
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include "Panel.hpp"
#include <cstdint>
#include <cstddef>

namespace Gui {

    class HeapDebugger : public Panel {
        uint32_t m_lastUpdateTime;
        
        // Cached stats
        int m_totalBlocks;
        int m_freeBlocks;
        size_t m_totalFreeMemory;
        size_t m_largestFreeBlock;
        size_t m_totalAllocated;

    public:
        HeapDebugger(int x, int y, int w, int h);

        void Render() override;
        void Update() override;

    private:
        void UpdateStats();
        void DrawStats();
    };

}
