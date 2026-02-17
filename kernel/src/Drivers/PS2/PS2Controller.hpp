/*
    * PS2Controller.hpp
    * PS/2 Controller (8042) initialization and utility functions
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <cstdint>

namespace Drivers::PS2 {

    // PS/2 controller I/O ports
    constexpr uint16_t DataPort    = 0x60;
    constexpr uint16_t StatusPort  = 0x64;
    constexpr uint16_t CommandPort = 0x64;

    // PS/2 controller commands
    constexpr uint8_t CmdReadConfig      = 0x20;
    constexpr uint8_t CmdWriteConfig     = 0x60;
    constexpr uint8_t CmdDisablePort2    = 0xA7;
    constexpr uint8_t CmdEnablePort2     = 0xA8;
    constexpr uint8_t CmdTestPort2       = 0xA9;
    constexpr uint8_t CmdSelfTest        = 0xAA;
    constexpr uint8_t CmdTestPort1       = 0xAB;
    constexpr uint8_t CmdDisablePort1    = 0xAD;
    constexpr uint8_t CmdEnablePort1     = 0xAE;
    constexpr uint8_t CmdWritePort2Input = 0xD4;

    // PS/2 controller status register bits
    constexpr uint8_t StatusOutputFull = 0x01;
    constexpr uint8_t StatusInputFull  = 0x02;

    // PS/2 controller self-test result
    constexpr uint8_t SelfTestPass = 0x55;
    constexpr uint8_t PortTestPass = 0x00;

    // Configuration byte bits
    constexpr uint8_t ConfigPort1Interrupt  = (1 << 0);
    constexpr uint8_t ConfigPort2Interrupt  = (1 << 1);
    constexpr uint8_t ConfigPort1Clock      = (1 << 4);
    constexpr uint8_t ConfigPort2Clock      = (1 << 5);
    constexpr uint8_t ConfigPort1Translation = (1 << 6);

    void Initialize();

    void SendCommand(uint8_t command);
    void SendData(uint8_t data);
    uint8_t ReadData();
    void WaitForInput();
    void WaitForOutput();
    void FlushOutputBuffer();

    void SendToPort2(uint8_t data);

    bool IsDualChannel();

};