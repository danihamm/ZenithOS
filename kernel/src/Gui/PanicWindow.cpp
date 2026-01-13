/*
    * PanicWindow.cpp
    * Specialized window for displaying Kernel Panic information
    * Copyright (c) 2025 Daniel Hammer
*/

#include "PanicWindow.hpp"
#include "Graphics.hpp"
#include "Color.hpp"
#include "Font.hpp"
#include <Libraries/String.hpp>

namespace Gui {

    PanicWindow::PanicWindow(const char* message, System::PanicFrame* frame)
        : Window(100, 100, 600, 600, "KERNEL PANIC", false) // Initial dummy pos, centered in Render or externally
        , m_frame(frame)
        , m_message(message)
    {
        // Center the window
        int screenW = GetScreenWidth();
        int screenH = GetScreenHeight();
        m_x = (screenW - m_width) / 2;
        m_y = (screenH - m_height) / 2;
    }

    void PanicWindow::DrawTableRow(int x, int y, int width, const char* label, const char* value, bool header) {
        int halfWidth = width / 2;
        uint32_t color = header ? Colors::Yellow : Colors::Text;
        uint32_t borderColor = Colors::TextDim;

        // Draw text
        DrawString(x + 5, y + 2, label, color, Colors::Surface);
        DrawString(x + halfWidth + 5, y + 2, value, Colors::Text, Colors::Surface);

        // Draw lines
        DrawHLine(x, y + LINE_HEIGHT, width, borderColor); // Bottom line
        DrawVLine(x + halfWidth, y, LINE_HEIGHT, borderColor); // Center separator
        
        // Outer borders are handled by previous calls or container
        DrawVLine(x, y, LINE_HEIGHT, borderColor);
        DrawVLine(x + width, y, LINE_HEIGHT, borderColor);
        
        if (header) {
             DrawHLine(x, y, width, borderColor); // Top line for header
        }
    }

    void PanicWindow::DrawTableRowHex(int x, int y, int width, const char* label, uint64_t value) {
        char buffer[32];
        const char* hexMap = "0123456789ABCDEF";
        
        // Manual hex conversion to avoid complex snprintf dependencies in panic
        buffer[0] = '0';
        buffer[1] = 'x';
        for(int i = 0; i < 16; i++) {
             buffer[17-i] = hexMap[(value >> (i*4)) & 0xF];
        }
        buffer[18] = '\0';
        
        DrawTableRow(x, y, width, label, buffer);
    }
    
    // Simple decimal conversion helper
    static void IntToDec(uint64_t val, char* out) {
        if (val == 0) {
            out[0] = '0'; out[1] = '\0'; return;
        }
        
        char temp[32];
        int i = 0;
        while (val > 0) {
            temp[i++] = (val % 10) + '0';
            val /= 10;
        }
        
        int j = 0;
        for (int k = i - 1; k >= 0; k--) {
            out[j++] = temp[k];
        }
        out[j] = '\0';
    }

    void PanicWindow::Render() {
        // Draw standard window frame
        Window::Render();

        int clientX = GetContentX() + PADDING;
        int clientY = GetContentY() + PADDING;
        int clientW = GetContentWidth() - (PADDING * 2);

        // 1. Draw "System halted" message
        DrawString(clientX, clientY, "System halted. Please reboot.", Colors::Red, Colors::Surface);
        clientY += LINE_HEIGHT * 2;

        // 2. Draw Meditation/Message
        DrawString(clientX, clientY, "Error Message:", Colors::Yellow, Colors::Surface);
        clientY += LINE_HEIGHT;
        DrawString(clientX, clientY, m_message, Colors::Text, Colors::Surface);
        clientY += LINE_HEIGHT * 2;

        // 3. Draw Register Table if frame exists
        if (m_frame) {
            // DrawString(clientX, clientY, "CPU State:", Colors::Cyan, Colors::Surface);
            clientY += LINE_HEIGHT;

            int tableY = clientY;
            
            // Working frame pointer (may be adjusted for exception frames)
            System::PanicFrame* displayFrame = m_frame;
            
            // Draw top border of the table
            DrawHLine(clientX, tableY, clientW, Colors::TextDim);

            // Interrupt Vector
            char vecBuf[4]; 
            IntToDec(m_frame->InterruptVector, vecBuf);
            DrawTableRow(clientX, tableY, clientW, "Interrupt Vector", vecBuf, false);
            tableY += LINE_HEIGHT;

            // Page Fault specific decoding (0xE)
            if (m_frame->InterruptVector == 0xE) {
                auto pf_frame = (System::PageFaultPanicFrame*)m_frame;
                displayFrame = (System::PanicFrame*)&pf_frame->IP; // Adjust for registers
                
                auto& err = pf_frame->PageFaultError;
                
                auto drawBit = [&](const char* label, uint8_t bit) {
                    DrawTableRow(clientX, tableY, clientW, label, bit ? "true" : "false");
                    tableY += LINE_HEIGHT;
                };

                drawBit("PF Present", err.Present);
                drawBit("PF Write", err.Write);
                drawBit("PF User", err.User);
                drawBit("PF Reserved Write", err.ReservedWrite);
                drawBit("PF Instr Fetch", err.InstructionFetch);
                drawBit("PF Prot Key", err.ProtectionKey);
                drawBit("PF Shadow Stack", err.ShadowStack);
                drawBit("PF SGX", err.SGX);
            } 
            // GPF specific decoding (0xD)
            else if (m_frame->InterruptVector == 0xD) {
                auto gpf_frame = (System::GPFPanicFrame*)m_frame;
                displayFrame = (System::PanicFrame*)&gpf_frame->IP; // Adjust for registers

                DrawTableRowHex(clientX, tableY, clientW, "GPF Error Code", gpf_frame->GeneralProtectionFaultError);
                tableY += LINE_HEIGHT;
            }

            // Standard Registers
            DrawTableRowHex(clientX, tableY, clientW, "Instruction Pointer", displayFrame->IP); tableY += LINE_HEIGHT;
            DrawTableRowHex(clientX, tableY, clientW, "Code Segment", displayFrame->CS); tableY += LINE_HEIGHT;
            DrawTableRowHex(clientX, tableY, clientW, "RFLAGS", displayFrame->Flags); tableY += LINE_HEIGHT;
            DrawTableRowHex(clientX, tableY, clientW, "Stack Pointer", displayFrame->SP); tableY += LINE_HEIGHT;
            DrawTableRowHex(clientX, tableY, clientW, "Stack Segment", displayFrame->SS); tableY += LINE_HEIGHT;
        }
    }

}
