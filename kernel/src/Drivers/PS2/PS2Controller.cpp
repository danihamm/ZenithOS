/*
    * PS2Controller.cpp
    * PS/2 Controller (8042) initialization and utility functions
    * Copyright (c) 2025 Daniel Hammer
*/

#include "PS2Controller.hpp"

#include <Io/IoPort.hpp>
#include <CppLib/Stream.hpp>
#include <Terminal/Terminal.hpp>

namespace Drivers::PS2 {

    static bool g_DualChannel = false;

    void WaitForInput() {
        // Wait until the input buffer is empty (bit 1 clear)
        int timeout = 100000;
        while ((Io::In8(StatusPort) & StatusInputFull) && --timeout) {
            Io::IoPortWait();
        }
    }

    void WaitForOutput() {
        // Wait until the output buffer is full (bit 0 set)
        int timeout = 100000;
        while (!(Io::In8(StatusPort) & StatusOutputFull) && --timeout) {
            Io::IoPortWait();
        }
    }

    void SendCommand(uint8_t command) {
        WaitForInput();
        Io::Out8(command, CommandPort);
    }

    void SendData(uint8_t data) {
        WaitForInput();
        Io::Out8(data, DataPort);
    }

    uint8_t ReadData() {
        WaitForOutput();
        return Io::In8(DataPort);
    }

    void FlushOutputBuffer() {
        // Read and discard any pending data in the output buffer
        int maxReads = 32;
        while ((Io::In8(StatusPort) & StatusOutputFull) && --maxReads) {
            Io::In8(DataPort);
            Io::IoPortWait();
        }
    }

    void SendToPort2(uint8_t data) {
        SendCommand(CmdWritePort2Input);
        SendData(data);
    }

    bool IsDualChannel() {
        return g_DualChannel;
    }

    void Initialize() {
        Kt::KernelLogStream(Kt::INFO, "PS2") << "Initializing PS/2 controller";

        // Step 1: Disable both PS/2 ports
        SendCommand(CmdDisablePort1);
        SendCommand(CmdDisablePort2);

        // Step 2: Flush the output buffer
        FlushOutputBuffer();

        // Step 3: Read the controller configuration byte
        SendCommand(CmdReadConfig);
        uint8_t config = ReadData();

        Kt::KernelLogStream(Kt::DEBUG, "PS2") << "Controller config byte: " << base::hex << (uint64_t)config;

        // Disable interrupts and translation in the config byte for now
        config &= ~(ConfigPort1Interrupt | ConfigPort2Interrupt | ConfigPort1Translation);

        // Check if this is a dual-channel controller
        // If bit 5 (port 2 clock) was set, it might be dual-channel
        g_DualChannel = (config & ConfigPort2Clock) != 0;

        // Step 4: Write the modified configuration byte
        SendCommand(CmdWriteConfig);
        SendData(config);

        // Step 5: Controller self-test
        SendCommand(CmdSelfTest);
        uint8_t selfTestResult = ReadData();

        if (selfTestResult != SelfTestPass) {
            Kt::KernelLogStream(Kt::ERROR, "PS2") << "Controller self-test failed: " << base::hex << (uint64_t)selfTestResult;
            return;
        }

        Kt::KernelLogStream(Kt::OK, "PS2") << "Controller self-test passed";

        // Self-test may reset the controller, so restore config
        SendCommand(CmdWriteConfig);
        SendData(config);

        // Step 6: Test port 2 to confirm dual-channel
        if (g_DualChannel) {
            SendCommand(CmdEnablePort2);

            SendCommand(CmdReadConfig);
            uint8_t config2 = ReadData();

            if (config2 & ConfigPort2Clock) {
                // Port 2 clock is still disabled after enabling -- not dual-channel
                g_DualChannel = false;
            } else {
                // It is dual-channel; disable port 2 again for testing
                SendCommand(CmdDisablePort2);
            }
        }

        // Step 7: Interface tests
        SendCommand(CmdTestPort1);
        uint8_t port1Test = ReadData();

        if (port1Test != PortTestPass) {
            Kt::KernelLogStream(Kt::ERROR, "PS2") << "Port 1 test failed: " << base::hex << (uint64_t)port1Test;
        } else {
            Kt::KernelLogStream(Kt::OK, "PS2") << "Port 1 (keyboard) test passed";
        }

        if (g_DualChannel) {
            SendCommand(CmdTestPort2);
            uint8_t port2Test = ReadData();

            if (port2Test != PortTestPass) {
                Kt::KernelLogStream(Kt::ERROR, "PS2") << "Port 2 test failed: " << base::hex << (uint64_t)port2Test;
                g_DualChannel = false;
            } else {
                Kt::KernelLogStream(Kt::OK, "PS2") << "Port 2 (mouse) test passed";
            }
        }

        // Step 8: Enable ports
        SendCommand(CmdEnablePort1);
        if (g_DualChannel) {
            SendCommand(CmdEnablePort2);
        }

        // Step 9: Enable interrupts in the configuration byte
        SendCommand(CmdReadConfig);
        config = ReadData();

        config |= ConfigPort1Interrupt | ConfigPort1Translation;
        if (g_DualChannel) {
            config |= ConfigPort2Interrupt;
        }

        SendCommand(CmdWriteConfig);
        SendData(config);

        Kt::KernelLogStream(Kt::OK, "PS2") << "Controller initialized (dual-channel: " << (g_DualChannel ? "yes" : "no") << ")";
    }

};