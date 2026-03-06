/*
    * Ahci.hpp
    * AHCI (Advanced Host Controller Interface) SATA driver
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <cstdint>
#include <Pci/Pci.hpp>

namespace Drivers::Storage::Ahci {

    // =========================================================================
    // AHCI Generic Host Control registers (HBA memory, offset from BAR5/ABAR)
    // =========================================================================

    constexpr uint32_t REG_CAP        = 0x00;  // Host Capabilities
    constexpr uint32_t REG_GHC        = 0x04;  // Global Host Control
    constexpr uint32_t REG_IS         = 0x08;  // Interrupt Status
    constexpr uint32_t REG_PI         = 0x0C;  // Ports Implemented
    constexpr uint32_t REG_VS         = 0x10;  // Version
    constexpr uint32_t REG_CAP2       = 0x24;  // Host Capabilities Extended
    constexpr uint32_t REG_BOHC       = 0x28;  // BIOS/OS Handoff Control

    // GHC register bits
    constexpr uint32_t GHC_HR         = (1u << 0);   // HBA Reset
    constexpr uint32_t GHC_IE         = (1u << 1);   // Interrupt Enable
    constexpr uint32_t GHC_AE         = (1u << 31);  // AHCI Enable

    // CAP register bits
    constexpr uint32_t CAP_S64A       = (1u << 31);  // Supports 64-bit Addressing
    constexpr uint32_t CAP_SSS        = (1u << 27);  // Supports Staggered Spin-up

    // BOHC register bits
    constexpr uint32_t BOHC_BOS       = (1u << 0);   // BIOS Owned Semaphore
    constexpr uint32_t BOHC_OOS       = (1u << 1);   // OS Owned Semaphore
    constexpr uint32_t BOHC_BB        = (1u << 4);   // BIOS Busy

    // =========================================================================
    // Per-port registers (base = 0x100 + port * 0x80)
    // =========================================================================

    constexpr uint32_t PORT_BASE      = 0x100;
    constexpr uint32_t PORT_SIZE      = 0x80;

    // Port register offsets (relative to port base)
    constexpr uint32_t PORT_CLB       = 0x00;  // Command List Base Address (low)
    constexpr uint32_t PORT_CLBU      = 0x04;  // Command List Base Address (high)
    constexpr uint32_t PORT_FB        = 0x08;  // FIS Base Address (low)
    constexpr uint32_t PORT_FBU       = 0x0C;  // FIS Base Address (high)
    constexpr uint32_t PORT_IS        = 0x10;  // Interrupt Status
    constexpr uint32_t PORT_IE        = 0x14;  // Interrupt Enable
    constexpr uint32_t PORT_CMD       = 0x18;  // Command and Status
    constexpr uint32_t PORT_TFD       = 0x20;  // Task File Data
    constexpr uint32_t PORT_SIG       = 0x24;  // Signature
    constexpr uint32_t PORT_SSTS      = 0x28;  // SATA Status (SCR0: SStatus)
    constexpr uint32_t PORT_SCTL      = 0x2C;  // SATA Control (SCR2: SControl)
    constexpr uint32_t PORT_SERR      = 0x30;  // SATA Error (SCR1: SError)
    constexpr uint32_t PORT_SACT      = 0x34;  // SATA Active
    constexpr uint32_t PORT_CI        = 0x38;  // Command Issue

    // PORT_CMD bits
    constexpr uint32_t PORT_CMD_ST    = (1u << 0);   // Start
    constexpr uint32_t PORT_CMD_SUD   = (1u << 1);   // Spin-Up Device
    constexpr uint32_t PORT_CMD_POD   = (1u << 2);   // Power On Device
    constexpr uint32_t PORT_CMD_FRE   = (1u << 4);   // FIS Receive Enable
    constexpr uint32_t PORT_CMD_FR    = (1u << 14);  // FIS Receive Running
    constexpr uint32_t PORT_CMD_CR    = (1u << 15);  // Command List Running
    constexpr uint32_t PORT_CMD_ICC_ACTIVE = (1u << 28); // Interface Comm Control: Active

    // PORT_TFD bits
    constexpr uint32_t PORT_TFD_BSY   = (1u << 7);   // Busy
    constexpr uint32_t PORT_TFD_DRQ   = (1u << 3);   // Data Request
    constexpr uint32_t PORT_TFD_ERR   = (1u << 0);   // Error

    // PORT_IS bits (interrupt status)
    constexpr uint32_t PORT_IS_DHRS   = (1u << 0);   // Device to Host Register FIS
    constexpr uint32_t PORT_IS_PSS    = (1u << 1);   // PIO Setup FIS
    constexpr uint32_t PORT_IS_DSS    = (1u << 2);   // DMA Setup FIS
    constexpr uint32_t PORT_IS_SDBS   = (1u << 3);   // Set Device Bits
    constexpr uint32_t PORT_IS_TFES   = (1u << 30);  // Task File Error Status

    // PORT_SSTS (SStatus) fields
    constexpr uint32_t SSTS_DET_MASK  = 0x0F;        // Device Detection
    constexpr uint32_t SSTS_DET_PRESENT = 0x03;      // Phy communication established

    // Device signatures
    constexpr uint32_t SIG_ATA        = 0x00000101;   // SATA drive
    constexpr uint32_t SIG_ATAPI      = 0xEB140101;   // SATAPI drive
    constexpr uint32_t SIG_SEMB       = 0xC33C0101;   // Enclosure management bridge
    constexpr uint32_t SIG_PM         = 0x96690101;   // Port multiplier

    // =========================================================================
    // FIS (Frame Information Structure) types
    // =========================================================================

    enum class FisType : uint8_t {
        RegH2D    = 0x27,  // Register FIS - Host to Device
        RegD2H    = 0x34,  // Register FIS - Device to Host
        DmaActivate = 0x39,
        DmaSetup  = 0x41,
        Data      = 0x46,
        BistActivate = 0x58,
        PioSetup  = 0x5F,
        DevBits   = 0xA1,
    };

    // Register FIS - Host to Device (used for issuing ATA commands)
    struct FisRegH2D {
        uint8_t  FisType;       // FisType::RegH2D (0x27)
        uint8_t  PmPort : 4;   // Port multiplier
        uint8_t  Reserved0 : 3;
        uint8_t  CmdCtl : 1;   // 1 = Command, 0 = Control
        uint8_t  Command;       // ATA command
        uint8_t  FeatureLow;    // Feature register (7:0)
        uint8_t  Lba0;          // LBA (7:0)
        uint8_t  Lba1;          // LBA (15:8)
        uint8_t  Lba2;          // LBA (23:16)
        uint8_t  Device;        // Device register
        uint8_t  Lba3;          // LBA (31:24)
        uint8_t  Lba4;          // LBA (39:32)
        uint8_t  Lba5;          // LBA (47:40)
        uint8_t  FeatureHigh;   // Feature register (15:8)
        uint16_t Count;         // Count
        uint8_t  Icc;           // Isochronous command completion
        uint8_t  Control;       // Control register
        uint32_t Reserved1;
    } __attribute__((packed));

    // =========================================================================
    // Command structures
    // =========================================================================

    // Physical Region Descriptor Table entry (16 bytes)
    struct PrdtEntry {
        uint32_t DataBaseLow;   // Data base address (low)
        uint32_t DataBaseHigh;  // Data base address (high)
        uint32_t Reserved;
        uint32_t ByteCount;     // Byte count (bit 31 = Interrupt on Completion)
    } __attribute__((packed));

    // Command Table (pointed to by command header, variable size)
    struct CommandTable {
        uint8_t  CommandFis[64];   // Command FIS (up to 64 bytes)
        uint8_t  AtapiCommand[16]; // ATAPI command (12 or 16 bytes)
        uint8_t  Reserved[48];     // Reserved
        PrdtEntry PrdtEntries[];   // PRDT entries (variable length)
    } __attribute__((packed));

    // Command Header (32 bytes each, 32 entries in Command List)
    struct CommandHeader {
        uint8_t  CflPmpA;       // Command FIS length (bits 4:0), PMP (bits 11:8), etc.
        uint8_t  Flags;         // Flags (Write bit 6, Prefetchable bit 7, etc.)
        uint16_t PrdtLength;    // PRDT entry count
        uint32_t PrdByteCount;  // PRD byte count transferred
        uint32_t CtbaLow;       // Command table base address (low)
        uint32_t CtbaHigh;      // Command table base address (high)
        uint32_t Reserved[4];
    } __attribute__((packed));

    // Command header flag bits
    constexpr uint8_t CMDHDR_WRITE    = (1u << 6);  // Write direction (in Flags byte)
    constexpr uint8_t CMDHDR_PREFETCH = (1u << 1);  // Prefetchable (in Flags byte)
    constexpr uint8_t CMDHDR_CLR_BSY  = (1u << 2);  // Clear Busy upon R_OK (in Flags byte)

    // =========================================================================
    // ATA commands
    // =========================================================================

    constexpr uint8_t ATA_CMD_IDENTIFY      = 0xEC;
    constexpr uint8_t ATA_CMD_READ_DMA_EX   = 0x25;  // READ DMA EXT (48-bit LBA)
    constexpr uint8_t ATA_CMD_WRITE_DMA_EX  = 0x35;  // WRITE DMA EXT (48-bit LBA)

    // =========================================================================
    // Constants
    // =========================================================================

    constexpr int MAX_PORTS          = 32;
    constexpr int CMD_HEADER_COUNT   = 32;    // 32 command slots per port
    constexpr int MAX_PRDT_ENTRIES   = 8;     // Max PRDT entries per command
    constexpr int SECTOR_SIZE        = 512;

    // MSI configuration
    constexpr uint8_t  MSI_IRQ       = 25;    // IRQ slot 25 = vector 57
    constexpr uint32_t MSI_VECTOR    = 57;
    constexpr uint32_t MSI_ADDR_BASE = 0xFEE00000;

    // =========================================================================
    // Port info
    // =========================================================================

    enum class PortType : uint8_t {
        None,
        Sata,
        Satapi,
        Semb,
        PortMultiplier,
    };

    struct PortInfo {
        bool     Active;
        PortType Type;
        uint64_t SectorCount;     // Total sectors (from IDENTIFY)
        char     Model[41];       // Model string (from IDENTIFY)
        char     Serial[21];      // Serial number (from IDENTIFY)
        char     Firmware[9];     // Firmware revision (from IDENTIFY)
        uint8_t  PortIndex;       // AHCI port number

        // Feature flags (from IDENTIFY)
        bool     SupportsLba48;
        bool     SupportsNcq;
        bool     SupportsTrim;
        bool     SupportsSmartSelfTest;
        bool     SupportsSmart;
        bool     SupportsWriteCache;
        bool     SupportsReadAhead;

        uint16_t SataGen;         // SATA generation (1/2/3)
        uint16_t NcqDepth;        // NCQ queue depth (0 if no NCQ)
        uint16_t SectorSizeLog;   // Logical sector size (bytes, usually 512)
        uint16_t SectorSizePhys;  // Physical sector size (bytes, 512 or 4096)
        uint16_t Rpm;             // Nominal RPM (0 = unknown, 1 = SSD/non-rotating)

        // DMA structures (physical + virtual)
        CommandHeader* CmdList;   // Command list (1 KiB, 32 headers)
        uint64_t       CmdListPhys;
        void*          FisArea;   // Received FIS area (256 bytes)
        uint64_t       FisAreaPhys;
        CommandTable*  CmdTables[CMD_HEADER_COUNT]; // Command tables
        uint64_t       CmdTablesPhys[CMD_HEADER_COUNT];
    };

    // =========================================================================
    // Public API
    // =========================================================================

    // Probe a PCI device (called by driver matching framework)
    bool Probe(const Pci::PciDevice& dev);

    // Check if the driver was initialized
    bool IsInitialized();

    // Get number of active ports with SATA devices
    int GetPortCount();

    // Read sectors from a SATA device
    // port: port index (0-31), lba: starting LBA, count: sector count (max 128)
    // buffer: destination buffer (must be large enough for count * 512 bytes)
    // Returns true on success
    bool ReadSectors(int port, uint64_t lba, uint32_t count, void* buffer);

    // Write sectors to a SATA device
    bool WriteSectors(int port, uint64_t lba, uint32_t count, const void* buffer);

    // Get info about a specific port
    const PortInfo* GetPortInfo(int port);

    // Get the total sector count for a port
    uint64_t GetSectorCount(int port);

};
