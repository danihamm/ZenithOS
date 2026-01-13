/*
    * HeapDebugger.cpp
    * Heap status visualization implementation
    * Copyright (c) 2025 Daniel Hammer
*/

#include "HeapDebugger.hpp"
#include "Graphics.hpp"
#include "Font.hpp"
#include "Color.hpp"
#include <Memory/Heap.hpp>

namespace Gui {

    HeapDebugger::HeapDebugger(int x, int y, int w, int h)
        : Panel(x, y, w, h, "Heap Debugger", true)
        , m_lastUpdateTime(0)
        , m_totalBlocks(0)
        , m_freeBlocks(0)
        , m_totalFreeMemory(0)
        , m_largestFreeBlock(0)
        , m_totalAllocated(0)
    {
    }

    void HeapDebugger::Render() {
        // Draw panel background
        FillRect(m_x, m_y, m_width, m_height, Colors::Surface);
        
        // Draw frame
        DrawFrame();
        
        // Draw title
        if (m_title) {
            DrawStringTransparent(m_x + 8, m_y + 5, m_title, Colors::Text);
        }
        
        // Draw separator
        DrawHLine(m_x + 1, m_y + 24, m_width - 2, Colors::Border);
        
        // Draw stats
        DrawStats();
    }

    void HeapDebugger::Update() {
        UpdateStats();
        MarkDirty();
    }

    void HeapDebugger::UpdateStats() {
        // Walk the heap freelist to gather statistics
        if (!Memory::g_heap) {
            m_totalBlocks = 0;
            m_freeBlocks = 0;
            m_totalFreeMemory = 0;
            m_largestFreeBlock = 0;
            return;
        }
        
        m_totalBlocks = Memory::g_heap->GetTotalBlocks();
        m_freeBlocks = Memory::g_heap->GetFreeBlocks();
        m_totalFreeMemory = Memory::g_heap->GetTotalFreeMemory();
        m_largestFreeBlock = Memory::g_heap->GetLargestFreeBlock();
        m_totalAllocated = Memory::g_heap->GetTotalAllocated();
    }

    void HeapDebugger::DrawStats() {
        int contentX = GetContentX() + 8;
        int contentY = m_y + 24 + 12;  // Title bar (24px) + padding (12px)
        int lineHeight = 18;
        int currentY = contentY;
        
        // Helper to draw a stat line
        auto drawStat = [&](const char* label, uint64_t value, bool isBytes = false) {
            DrawStringTransparent(contentX, currentY, label, Colors::TextDim);
            
            // Format value
            char valueStr[32];
            if (isBytes) {
                // Format as KB/MB
                if (value >= 1024 * 1024) {
                    uint64_t mb = value / (1024 * 1024);
                    uint64_t kb = (value % (1024 * 1024)) / 1024;
                    // Simple sprintf alternative
                    int i = 0;
                    uint64_t temp = mb;
                    if (temp == 0) {
                        valueStr[i++] = '0';
                    } else {
                        char digits[20];
                        int j = 0;
                        while (temp > 0) {
                            digits[j++] = '0' + (temp % 10);
                            temp /= 10;
                        }
                        while (j > 0) {
                            valueStr[i++] = digits[--j];
                        }
                    }
                    valueStr[i++] = ' ';
                    valueStr[i++] = 'M';
                    valueStr[i++] = 'B';
                    valueStr[i] = '\0';
                } else if (value >= 1024) {
                    uint64_t kb = value / 1024;
                    int i = 0;
                    uint64_t temp = kb;
                    if (temp == 0) {
                        valueStr[i++] = '0';
                    } else {
                        char digits[20];
                        int j = 0;
                        while (temp > 0) {
                            digits[j++] = '0' + (temp % 10);
                            temp /= 10;
                        }
                        while (j > 0) {
                            valueStr[i++] = digits[--j];
                        }
                    }
                    valueStr[i++] = ' ';
                    valueStr[i++] = 'K';
                    valueStr[i++] = 'B';
                    valueStr[i] = '\0';
                } else {
                    int i = 0;
                    uint64_t temp = value;
                    if (temp == 0) {
                        valueStr[i++] = '0';
                    } else {
                        char digits[20];
                        int j = 0;
                        while (temp > 0) {
                            digits[j++] = '0' + (temp % 10);
                            temp /= 10;
                        }
                        while (j > 0) {
                            valueStr[i++] = digits[--j];
                        }
                    }
                    valueStr[i++] = ' ';
                    valueStr[i++] = 'B';
                    valueStr[i] = '\0';
                }
            } else {
                // Format as decimal number
                int i = 0;
                uint64_t temp = value;
                if (temp == 0) {
                    valueStr[i++] = '0';
                } else {
                    char digits[20];
                    int j = 0;
                    while (temp > 0) {
                        digits[j++] = '0' + (temp % 10);
                        temp /= 10;
                    }
                    while (j > 0) {
                        valueStr[i++] = digits[--j];
                    }
                }
                valueStr[i] = '\0';
            }
            
            DrawStringTransparent(contentX + 120, currentY, valueStr, Colors::Text);
            currentY += lineHeight;
        };
        
        // Draw statistics
        drawStat("Free Blocks:", m_freeBlocks, false);
        drawStat("Total Blocks:", m_totalBlocks, false);
        drawStat("Free Memory:", m_totalFreeMemory, true);
        drawStat("Largest Free:", m_largestFreeBlock, true);
        drawStat("Total Alloc:", m_totalAllocated, true);
        
        // Draw status indicator
        currentY += 8;
        DrawStringTransparent(contentX, currentY, "Status:", Colors::TextDim);
        
        uint32_t statusColor = Memory::g_heap ? Colors::Green : Colors::Red;
        const char* statusText = Memory::g_heap ? "Active" : "Not Init";
        DrawStringTransparent(contentX + 120, currentY, statusText, statusColor);
    }

}
