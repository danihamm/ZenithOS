/*
    * DebugGui.hpp
    * Main debug GUI interface
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include "LogWindow.hpp"
#include <cstdint>
#include <limine.h>

namespace Gui {

    // Initialize the debug GUI system
    void Init(limine_framebuffer* fb);

    // Check if GUI is initialized
    bool IsInitialized();

    // Log a message to the GUI
    void Log(LogLevel level, const char* component, const char* message);

    // Update time display in the top bar
    void UpdateTime(uint16_t year, uint8_t month, uint8_t day,
                    uint8_t hour, uint8_t minute, uint8_t second);

    // Force a full redraw of the GUI
    void Refresh();

    // Update the GUI (render dirty panels)
    void Update();

    class GuiLogStream {
    public:
        enum Base {
            Dec = 10,
            Hex = 16
        };

        struct BaseManip {
            Base base;
        };

    private:
        static constexpr int BUFFER_SIZE = 256;
        char buffer[BUFFER_SIZE];
        int pos;
        LogLevel level;
        const char* component;
        Base currentBase;

        void Append(const char* str) {
            while (*str && pos < BUFFER_SIZE - 1) {
                buffer[pos++] = *str++;
            }
        }

        // Helper to convert number to string and append
        void AppendNumber(uint64_t num, Base base) {
            char temp[32];
            int i = 0;
            
            if (num == 0) {
                temp[i++] = '0';
            } else {
                while (num > 0 && i < 31) {
                    int digit = num % base;
                    temp[i++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
                    num /= base;
                }
            }
            
            // Reverse and append
            while (i > 0 && pos < BUFFER_SIZE - 1) {
                buffer[pos++] = temp[--i];
            }
        }

    public:
        GuiLogStream(LogLevel lvl, const char* comp) 
            : pos(0), level(lvl), component(comp), currentBase(Dec) {
            buffer[0] = '\0';
        }

        ~GuiLogStream() {
            buffer[pos] = '\0';
            Log(level, component, buffer);
        }

        // Stream operators for various types
        GuiLogStream& operator<<(const char* str) {
            Append(str);
            return *this;
        }

        GuiLogStream& operator<<(char c) {
            if (pos < BUFFER_SIZE - 1) {
                buffer[pos++] = c;
            }
            return *this;
        }

        GuiLogStream& operator<<(int num) {
            if (num < 0) {
                if (pos < BUFFER_SIZE - 1) {
                    buffer[pos++] = '-';
                }
                num = -num;
            }
            AppendNumber(num, currentBase);
            return *this;
        }

        GuiLogStream& operator<<(uint32_t num) {
            AppendNumber(num, currentBase);
            return *this;
        }

        GuiLogStream& operator<<(uint64_t num) {
            AppendNumber(num, currentBase);
            return *this;
        }

        GuiLogStream& operator<<(BaseManip manip) {
            currentBase = manip.base;
            return *this;
        }
    };

    // Base manipulators
    namespace base {
        inline GuiLogStream::BaseManip hex() { return {GuiLogStream::Hex}; }
        inline GuiLogStream::BaseManip dec() { return {GuiLogStream::Dec}; }
    }

}

// Convenience macro for logging
#define GUI_LOG(level, component, msg) Gui::Log(Gui::LogLevel::level, component, msg)

