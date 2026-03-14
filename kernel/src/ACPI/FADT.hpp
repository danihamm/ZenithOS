/*
    * FADT.hpp
    * Fixed ACPI Description Table parsing
    * Copyright (c) 2026 Daniel Hammer
*/

#pragma once
#include <cstdint>
#include <ACPI/ACPI.hpp>

namespace Hal {
    namespace FADT {

        struct Table {
            ACPI::CommonSDTHeader Header;
            uint32_t FirmwareCtrl;
            uint32_t Dsdt;
            uint8_t  Reserved0;
            uint8_t  PreferredPMProfile;
            uint16_t SCI_Interrupt;
            uint32_t SMI_CommandPort;
            uint8_t  AcpiEnable;
            uint8_t  AcpiDisable;
            uint8_t  S4BIOS_REQ;
            uint8_t  PSTATE_Control;
            uint32_t PM1aEventBlock;
            uint32_t PM1bEventBlock;
            uint32_t PM1aControlBlock;
            uint32_t PM1bControlBlock;
            uint32_t PM2ControlBlock;
            uint32_t PMTimerBlock;
            uint32_t GPE0Block;
            uint32_t GPE1Block;
            uint8_t  PM1EventLength;
            uint8_t  PM1ControlLength;
            uint8_t  PM2ControlLength;
            uint8_t  PMTimerLength;
            uint8_t  GPE0Length;
            uint8_t  GPE1Length;
            uint8_t  GPE1Base;
            uint8_t  CStateControl;
            uint16_t WorstC2Latency;
            uint16_t WorstC3Latency;
            uint16_t FlushSize;
            uint16_t FlushStride;
            uint8_t  DutyOffset;
            uint8_t  DutyWidth;
            uint8_t  DayAlarm;
            uint8_t  MonthAlarm;
            uint8_t  Century;
            uint16_t BootArchitectureFlags;
            uint8_t  Reserved1;
            uint32_t Flags;
            // Generic Address Structure for RESET_REG (12 bytes)
            uint8_t  ResetReg[12];
            uint8_t  ResetValue;
            uint16_t ArmBootArchFlags;
            uint8_t  FADTMinorVersion;
            // Extended fields (ACPI 2.0+)
            uint64_t X_FirmwareControl;
            uint64_t X_Dsdt;
            uint8_t  X_PM1aEventBlock[12];
            uint8_t  X_PM1bEventBlock[12];
            uint8_t  X_PM1aControlBlock[12];
            uint8_t  X_PM1bControlBlock[12];
            uint8_t  X_PM2ControlBlock[12];
            uint8_t  X_PMTimerBlock[12];
            uint8_t  X_GPE0Block[12];
            uint8_t  X_GPE1Block[12];
        } __attribute__((packed));

        struct ParsedFADT {
            uint64_t DsdtAddress;
            uint64_t FacsAddress;
            uint32_t PM1aEventBlock;
            uint32_t PM1bEventBlock;
            uint32_t PM1aControlBlock;
            uint32_t PM1bControlBlock;
            uint32_t PM2ControlBlock;
            uint32_t PMTimerBlock;
            uint32_t GPE0Block;
            uint32_t GPE1Block;
            uint32_t SMI_CommandPort;
            uint16_t SCI_Interrupt;
            uint8_t  PM1EventLength;
            uint8_t  GPE0Length;
            uint8_t  GPE1Length;
            uint8_t  AcpiEnable;
            uint8_t  AcpiDisable;
            uint32_t Flags;
            bool     Valid;
        };

        bool Parse(ACPI::CommonSDTHeader* xsdt, ParsedFADT& result);
    };
};
