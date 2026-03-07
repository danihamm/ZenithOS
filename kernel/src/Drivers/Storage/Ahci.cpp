/*
    * Ahci.cpp
    * AHCI (Advanced Host Controller Interface) SATA driver
    * Copyright (c) 2025 Daniel Hammer
*/

#include "Ahci.hpp"
#include "BlockDevice.hpp"
#include <Pci/Pci.hpp>
#include <Terminal/Terminal.hpp>
#include <CppLib/Stream.hpp>
#include <Memory/HHDM.hpp>
#include <Memory/Paging.hpp>
#include <Memory/PageFrameAllocator.hpp>
#include <Libraries/Memory.hpp>
#include <Hal/Apic/Interrupts.hpp>
#include <Hal/Apic/IoApic.hpp>

using namespace Kt;

namespace Drivers::Storage::Ahci {

    // -------------------------------------------------------------------------
    // Driver state
    // -------------------------------------------------------------------------

    static bool g_initialized = false;
    static volatile uint8_t* g_mmioBase = nullptr;
    static uint32_t g_portsImplemented = 0;
    static int g_activePortCount = 0;
    static PortInfo g_ports[MAX_PORTS] = {};

    // -------------------------------------------------------------------------
    // Register access
    // -------------------------------------------------------------------------

    static void WriteReg(uint32_t reg, uint32_t value) {
        *(volatile uint32_t*)(g_mmioBase + reg) = value;
    }

    static uint32_t ReadReg(uint32_t reg) {
        return *(volatile uint32_t*)(g_mmioBase + reg);
    }

    static void WritePortReg(int port, uint32_t reg, uint32_t value) {
        WriteReg(PORT_BASE + port * PORT_SIZE + reg, value);
    }

    static uint32_t ReadPortReg(int port, uint32_t reg) {
        return ReadReg(PORT_BASE + port * PORT_SIZE + reg);
    }

    // -------------------------------------------------------------------------
    // DMA buffer allocation
    // -------------------------------------------------------------------------

    static void* AllocateDmaBuffer(uint64_t& outPhys, int pages = 1) {
        void* virt;
        if (pages == 1) {
            virt = Memory::g_pfa->AllocateZeroed();
        } else {
            virt = Memory::g_pfa->ReallocConsecutive(nullptr, pages);
            memset(virt, 0, pages * 0x1000);
        }
        outPhys = Memory::SubHHDM(virt);
        return virt;
    }

    // -------------------------------------------------------------------------
    // BIOS/OS Handoff
    // -------------------------------------------------------------------------

    static void PerformBiosHandoff() {
        uint32_t cap2 = ReadReg(REG_CAP2);
        if (!(cap2 & (1u << 0))) {
            return; // BOH not supported
        }

        uint32_t bohc = ReadReg(REG_BOHC);
        if (!(bohc & BOHC_BOS)) {
            return; // BIOS doesn't own it
        }

        KernelLogStream(INFO, "AHCI") << "Performing BIOS/OS handoff...";

        // Set OS Owned Semaphore
        WriteReg(REG_BOHC, bohc | BOHC_OOS);

        // Wait for BIOS to release (up to 25ms per spec)
        for (int i = 0; i < 250000; i++) {
            bohc = ReadReg(REG_BOHC);
            if (!(bohc & BOHC_BOS)) {
                KernelLogStream(OK, "AHCI") << "BIOS handoff complete";
                return;
            }
            asm volatile("" ::: "memory");
        }

        // If BIOS Busy, wait another 2 seconds
        if (ReadReg(REG_BOHC) & BOHC_BB) {
            for (int i = 0; i < 2000000; i++) {
                if (!(ReadReg(REG_BOHC) & BOHC_BB)) break;
                asm volatile("" ::: "memory");
            }
        }

        KernelLogStream(WARNING, "AHCI") << "BIOS handoff timed out, proceeding anyway";
    }

    // -------------------------------------------------------------------------
    // Port engine control
    // -------------------------------------------------------------------------

    static void StopPort(int port) {
        uint32_t cmd = ReadPortReg(port, PORT_CMD);

        // Clear ST (Stop command engine)
        if (cmd & PORT_CMD_ST) {
            cmd &= ~PORT_CMD_ST;
            WritePortReg(port, PORT_CMD, cmd);
        }

        // Clear FRE (Stop FIS receive)
        if (cmd & PORT_CMD_FRE) {
            cmd &= ~PORT_CMD_FRE;
            WritePortReg(port, PORT_CMD, cmd);
        }

        // Wait for CR and FR to clear
        for (int i = 0; i < 500000; i++) {
            cmd = ReadPortReg(port, PORT_CMD);
            if (!(cmd & PORT_CMD_CR) && !(cmd & PORT_CMD_FR)) {
                return;
            }
            asm volatile("" ::: "memory");
        }

        KernelLogStream(WARNING, "AHCI") << "Port " << port << " stop timed out";
    }

    static void StartPort(int port) {
        // Wait for CR to clear before starting
        for (int i = 0; i < 500000; i++) {
            if (!(ReadPortReg(port, PORT_CMD) & PORT_CMD_CR)) break;
            asm volatile("" ::: "memory");
        }

        uint32_t cmd = ReadPortReg(port, PORT_CMD);
        cmd |= PORT_CMD_FRE;
        WritePortReg(port, PORT_CMD, cmd);

        cmd |= PORT_CMD_ST;
        WritePortReg(port, PORT_CMD, cmd);
    }

    // -------------------------------------------------------------------------
    // Port detection and classification
    // -------------------------------------------------------------------------

    static PortType ClassifyPort(int port) {
        uint32_t ssts = ReadPortReg(port, PORT_SSTS);
        uint32_t det = ssts & SSTS_DET_MASK;

        if (det != SSTS_DET_PRESENT) {
            return PortType::None;
        }

        // Wait for device to become ready (BSY clear) so the signature is valid.
        // After HBA/port reset, the device sends a D2H Register FIS with its
        // signature once it finishes COMRESET.  Until that FIS arrives PORT_SIG
        // may contain stale data.
        for (int i = 0; i < 1000000; i++) {
            uint32_t tfd = ReadPortReg(port, PORT_TFD);
            if (!(tfd & PORT_TFD_BSY)) break;
            asm volatile("" ::: "memory");
        }

        uint32_t sig = ReadPortReg(port, PORT_SIG);
        switch (sig) {
            case SIG_ATA:   return PortType::Sata;
            case SIG_ATAPI: return PortType::Satapi;
            case SIG_SEMB:  return PortType::Semb;
            case SIG_PM:    return PortType::PortMultiplier;
            default:        return PortType::None;
        }
    }

    // -------------------------------------------------------------------------
    // Port initialization (allocate command list, FIS area, command tables)
    // -------------------------------------------------------------------------

    static void InitPort(int port) {
        StopPort(port);

        // Allocate Command List (1 KiB, 1024-byte aligned)
        // and FIS area (256 bytes, 256-byte aligned)
        // Both fit in one 4 KiB page
        uint64_t clPhys;
        void* clVirt = AllocateDmaBuffer(clPhys);
        g_ports[port].CmdList = (CommandHeader*)clVirt;
        g_ports[port].CmdListPhys = clPhys;

        // FIS area goes in the second half of the same page
        uint64_t fbPhys = clPhys + 1024;
        void* fbVirt = (void*)((uint8_t*)clVirt + 1024);
        g_ports[port].FisArea = fbVirt;
        g_ports[port].FisAreaPhys = fbPhys;

        // Set CLB and FB in port registers
        WritePortReg(port, PORT_CLB, (uint32_t)(clPhys & 0xFFFFFFFF));
        WritePortReg(port, PORT_CLBU, (uint32_t)(clPhys >> 32));
        WritePortReg(port, PORT_FB, (uint32_t)(fbPhys & 0xFFFFFFFF));
        WritePortReg(port, PORT_FBU, (uint32_t)(fbPhys >> 32));

        // Allocate command tables (one page each, 128-byte aligned)
        CommandHeader* headers = g_ports[port].CmdList;
        for (int i = 0; i < CMD_HEADER_COUNT; i++) {
            uint64_t ctPhys;
            void* ctVirt = AllocateDmaBuffer(ctPhys);
            g_ports[port].CmdTables[i] = (CommandTable*)ctVirt;
            g_ports[port].CmdTablesPhys[i] = ctPhys;

            headers[i].CtbaLow = (uint32_t)(ctPhys & 0xFFFFFFFF);
            headers[i].CtbaHigh = (uint32_t)(ctPhys >> 32);
            headers[i].PrdtLength = 0;
            headers[i].PrdByteCount = 0;
        }

        // Clear interrupt status and error
        WritePortReg(port, PORT_SERR, 0xFFFFFFFF);
        WritePortReg(port, PORT_IS, 0xFFFFFFFF);

        // Enable interrupts for this port
        WritePortReg(port, PORT_IE,
            PORT_IS_DHRS | PORT_IS_PSS | PORT_IS_DSS |
            PORT_IS_SDBS | PORT_IS_TFES);

        // Power on and spin up if needed
        uint32_t cmd = ReadPortReg(port, PORT_CMD);
        cmd |= PORT_CMD_POD | PORT_CMD_SUD;
        WritePortReg(port, PORT_CMD, cmd);

        StartPort(port);
    }

    // -------------------------------------------------------------------------
    // Issue a command and wait for completion
    // -------------------------------------------------------------------------

    static bool IssueCommand(int port, int slot) {
        // Wait for the slot to be free
        for (int i = 0; i < 500000; i++) {
            if (!(ReadPortReg(port, PORT_CI) & (1u << slot))) break;
            asm volatile("" ::: "memory");
        }

        // Check that the port isn't in error
        uint32_t tfd = ReadPortReg(port, PORT_TFD);
        if (tfd & (PORT_TFD_BSY | PORT_TFD_DRQ)) {
            // Device is busy, wait
            for (int i = 0; i < 1000000; i++) {
                tfd = ReadPortReg(port, PORT_TFD);
                if (!(tfd & (PORT_TFD_BSY | PORT_TFD_DRQ))) break;
                asm volatile("" ::: "memory");
            }
            if (tfd & (PORT_TFD_BSY | PORT_TFD_DRQ)) {
                KernelLogStream(ERROR, "AHCI") << "Port " << port << " device busy before command issue";
                return false;
            }
        }

        // Issue the command
        WritePortReg(port, PORT_CI, 1u << slot);

        // Wait for completion
        for (int i = 0; i < 5000000; i++) {
            uint32_t ci = ReadPortReg(port, PORT_CI);
            if (!(ci & (1u << slot))) {
                // Check for errors
                uint32_t is = ReadPortReg(port, PORT_IS);
                if (is & PORT_IS_TFES) {
                    KernelLogStream(ERROR, "AHCI") << "Port " << port << " task file error";
                    WritePortReg(port, PORT_IS, PORT_IS_TFES);
                    return false;
                }
                // Clear interrupt status
                WritePortReg(port, PORT_IS, is);
                return true;
            }

            // Check for fatal error during wait
            uint32_t is = ReadPortReg(port, PORT_IS);
            if (is & PORT_IS_TFES) {
                KernelLogStream(ERROR, "AHCI") << "Port " << port
                    << " task file error during command, TFD=" << base::hex << (uint64_t)ReadPortReg(port, PORT_TFD);
                WritePortReg(port, PORT_IS, PORT_IS_TFES);
                return false;
            }

            asm volatile("" ::: "memory");
        }

        KernelLogStream(ERROR, "AHCI") << "Port " << port << " command timeout";
        return false;
    }

    // -------------------------------------------------------------------------
    // Build a command FIS for a DMA read/write
    // -------------------------------------------------------------------------

    static int FindFreeSlot(int port) {
        uint32_t slots = ReadPortReg(port, PORT_SACT) | ReadPortReg(port, PORT_CI);
        for (int i = 0; i < CMD_HEADER_COUNT; i++) {
            if (!(slots & (1u << i))) {
                return i;
            }
        }
        return -1;
    }

    static void BuildReadWriteCommand(int port, int slot, uint64_t lba, uint32_t count,
                                       uint64_t bufferPhys, bool write) {
        CommandHeader* hdr = &g_ports[port].CmdList[slot];
        CommandTable* tbl = g_ports[port].CmdTables[slot];

        // Clear the command table
        memset(tbl, 0, sizeof(CommandTable) + sizeof(PrdtEntry) * MAX_PRDT_ENTRIES);

        // Build FIS
        FisRegH2D* fis = (FisRegH2D*)tbl->CommandFis;
        fis->FisType = (uint8_t)FisType::RegH2D;
        fis->CmdCtl = 1;  // Command
        fis->Command = write ? ATA_CMD_WRITE_DMA_EX : ATA_CMD_READ_DMA_EX;
        fis->Device = (1 << 6); // LBA mode

        fis->Lba0 = (uint8_t)(lba);
        fis->Lba1 = (uint8_t)(lba >> 8);
        fis->Lba2 = (uint8_t)(lba >> 16);
        fis->Lba3 = (uint8_t)(lba >> 24);
        fis->Lba4 = (uint8_t)(lba >> 32);
        fis->Lba5 = (uint8_t)(lba >> 40);

        fis->Count = (uint16_t)count;

        // Build PRDT entries
        uint32_t bytesRemaining = count * SECTOR_SIZE;
        int prdtIdx = 0;
        uint64_t currentPhys = bufferPhys;

        while (bytesRemaining > 0 && prdtIdx < MAX_PRDT_ENTRIES) {
            uint32_t chunkSize = bytesRemaining;
            // Each PRDT entry can transfer up to 4 MiB (aligned to word boundary)
            if (chunkSize > 0x400000) {
                chunkSize = 0x400000;
            }

            tbl->PrdtEntries[prdtIdx].DataBaseLow = (uint32_t)(currentPhys & 0xFFFFFFFF);
            tbl->PrdtEntries[prdtIdx].DataBaseHigh = (uint32_t)(currentPhys >> 32);
            tbl->PrdtEntries[prdtIdx].Reserved = 0;
            // Byte count is (actual bytes - 1), bit 31 = interrupt on completion for last entry
            tbl->PrdtEntries[prdtIdx].ByteCount = (chunkSize - 1);
            if (bytesRemaining <= chunkSize) {
                tbl->PrdtEntries[prdtIdx].ByteCount |= (1u << 31); // IOC
            }

            currentPhys += chunkSize;
            bytesRemaining -= chunkSize;
            prdtIdx++;
        }

        // Set up command header
        // CFL = FIS length in dwords (5 for FisRegH2D = 20 bytes / 4)
        hdr->CflPmpA = 5; // CFL bits [4:0]
        hdr->Flags = write ? CMDHDR_WRITE : 0;
        hdr->PrdtLength = (uint16_t)prdtIdx;
        hdr->PrdByteCount = 0;
    }

    // -------------------------------------------------------------------------
    // IDENTIFY DEVICE
    // -------------------------------------------------------------------------

    static void SwapStringBytes(char* str, int len) {
        for (int i = 0; i < len; i += 2) {
            char tmp = str[i];
            str[i] = str[i + 1];
            str[i + 1] = tmp;
        }
        // Trim trailing spaces
        for (int i = len - 1; i >= 0; i--) {
            if (str[i] == ' ' || str[i] == '\0') {
                str[i] = '\0';
            } else {
                break;
            }
        }
    }

    static bool IdentifyDevice(int port) {
        int slot = FindFreeSlot(port);
        if (slot < 0) {
            KernelLogStream(ERROR, "AHCI") << "Port " << port << " no free command slot for IDENTIFY";
            return false;
        }

        // Allocate a page for IDENTIFY data (512 bytes)
        uint64_t identPhys;
        uint16_t* identData = (uint16_t*)AllocateDmaBuffer(identPhys);

        CommandHeader* hdr = &g_ports[port].CmdList[slot];
        CommandTable* tbl = g_ports[port].CmdTables[slot];

        memset(tbl, 0, sizeof(CommandTable) + sizeof(PrdtEntry));

        FisRegH2D* fis = (FisRegH2D*)tbl->CommandFis;
        fis->FisType = (uint8_t)FisType::RegH2D;
        fis->CmdCtl = 1;
        fis->Command = ATA_CMD_IDENTIFY;
        fis->Device = 0;

        tbl->PrdtEntries[0].DataBaseLow = (uint32_t)(identPhys & 0xFFFFFFFF);
        tbl->PrdtEntries[0].DataBaseHigh = (uint32_t)(identPhys >> 32);
        tbl->PrdtEntries[0].ByteCount = 511 | (1u << 31); // 512 bytes, IOC

        hdr->CflPmpA = 5;
        hdr->Flags = 0;
        hdr->PrdtLength = 1;
        hdr->PrdByteCount = 0;

        if (!IssueCommand(port, slot)) {
            KernelLogStream(ERROR, "AHCI") << "Port " << port << " IDENTIFY failed";
            Memory::g_pfa->Free(identData);
            return false;
        }

        // Parse IDENTIFY data
        // Word 100-103: Total number of user addressable sectors (48-bit LBA)
        uint64_t sectors = (uint64_t)identData[100]
                         | ((uint64_t)identData[101] << 16)
                         | ((uint64_t)identData[102] << 32)
                         | ((uint64_t)identData[103] << 48);

        if (sectors == 0) {
            // Fallback to 28-bit LBA sector count (words 60-61)
            sectors = (uint64_t)identData[60] | ((uint64_t)identData[61] << 16);
        }

        g_ports[port].SectorCount = sectors;
        g_ports[port].PortIndex = (uint8_t)port;

        // Model string: words 27-46 (40 bytes)
        memcpy(g_ports[port].Model, &identData[27], 40);
        g_ports[port].Model[40] = '\0';
        SwapStringBytes(g_ports[port].Model, 40);

        // Serial number: words 10-19 (20 bytes)
        memcpy(g_ports[port].Serial, &identData[10], 20);
        g_ports[port].Serial[20] = '\0';
        SwapStringBytes(g_ports[port].Serial, 20);

        // Firmware revision: words 23-26 (8 bytes)
        memcpy(g_ports[port].Firmware, &identData[23], 8);
        g_ports[port].Firmware[8] = '\0';
        SwapStringBytes(g_ports[port].Firmware, 8);

        // Word 49: capabilities — bit 9 = LBA supported
        // Word 83: command set supported — bit 10 = 48-bit LBA
        uint16_t cmdSet83 = identData[83];
        g_ports[port].SupportsLba48 = (cmdSet83 & (1 << 10)) != 0;

        // Word 82: command set supported
        uint16_t cmdSet82 = identData[82];
        g_ports[port].SupportsSmart = (cmdSet82 & (1 << 0)) != 0;
        g_ports[port].SupportsWriteCache = (cmdSet82 & (1 << 5)) != 0;
        g_ports[port].SupportsReadAhead = (cmdSet82 & (1 << 6)) != 0;

        // Word 84: command/feature set supported ext — bit 1 = SMART self-test
        uint16_t cmdSet84 = identData[84];
        g_ports[port].SupportsSmartSelfTest = (cmdSet84 & (1 << 1)) != 0;

        // Word 75: NCQ queue depth (bits 4:0 = max depth - 1)
        uint16_t word75 = identData[75];
        uint16_t sataCapabilities = identData[76]; // Word 76: SATA capabilities
        g_ports[port].SupportsNcq = (sataCapabilities & (1 << 8)) != 0;
        g_ports[port].NcqDepth = g_ports[port].SupportsNcq ? (uint16_t)((word75 & 0x1F) + 1) : 0;

        // Word 76: SATA capabilities — bits 3:1 = supported SATA generations
        g_ports[port].SataGen = 0;
        if (sataCapabilities & (1 << 3)) g_ports[port].SataGen = 3; // 6 Gbps
        else if (sataCapabilities & (1 << 2)) g_ports[port].SataGen = 2; // 3 Gbps
        else if (sataCapabilities & (1 << 1)) g_ports[port].SataGen = 1; // 1.5 Gbps

        // Word 169: TRIM support (Data Set Management)
        uint16_t word169 = identData[169];
        g_ports[port].SupportsTrim = (word169 & (1 << 0)) != 0;

        // Word 106: Physical/logical sector size
        uint16_t word106 = identData[106];
        if (word106 & (1 << 14)) {
            // Valid field
            g_ports[port].SectorSizePhys = (word106 & (1 << 12))
                ? (uint16_t)(512 * (1 << (word106 & 0x0F))) : 512;
            g_ports[port].SectorSizeLog = (word106 & (1 << 13)) ? 0 : 512;
            // If logical sector size specified in words 117-118
            if (word106 & (1 << 13)) {
                g_ports[port].SectorSizeLog = (uint16_t)(
                    ((uint32_t)identData[117] | ((uint32_t)identData[118] << 16)) * 2);
            }
        } else {
            g_ports[port].SectorSizeLog = 512;
            g_ports[port].SectorSizePhys = 512;
        }
        if (g_ports[port].SectorSizeLog == 0) g_ports[port].SectorSizeLog = 512;

        // Word 217: Nominal Media Rotation Rate
        // 0000h = not reported, 0001h = non-rotating (SSD), else RPM
        g_ports[port].Rpm = identData[217];

        Memory::g_pfa->Free(identData);

        uint64_t sizeBytes = sectors * SECTOR_SIZE;
        uint64_t sizeMB = sizeBytes / (1024 * 1024);
        uint64_t sizeGB = sizeMB / 1024;

        if (sizeGB > 0) {
            KernelLogStream(OK, "AHCI") << "Port " << port << ": " << g_ports[port].Model
                << " (" << sizeGB << " GiB)";
        } else {
            KernelLogStream(OK, "AHCI") << "Port " << port << ": " << g_ports[port].Model
                << " (" << sizeMB << " MiB)";
        }

        return true;
    }

    // -------------------------------------------------------------------------
    // Interrupt handler
    // -------------------------------------------------------------------------

    static void HandleInterrupt(uint8_t irq) {
        (void)irq;

        uint32_t is = ReadReg(REG_IS);
        if (is == 0) return;

        // Acknowledge each port's interrupt
        for (int i = 0; i < MAX_PORTS; i++) {
            if (is & (1u << i)) {
                uint32_t portIs = ReadPortReg(i, PORT_IS);
                WritePortReg(i, PORT_IS, portIs);
            }
        }

        // Clear global interrupt status
        WriteReg(REG_IS, is);
    }

    // -------------------------------------------------------------------------
    // MSI setup
    // -------------------------------------------------------------------------

    static bool SetupMsi(uint8_t bus, uint8_t dev, uint8_t func) {
        uint8_t cap = Pci::FindCapability(bus, dev, func, Pci::PCI_CAP_MSI);
        if (cap == 0) {
            KernelLogStream(INFO, "AHCI") << "MSI capability not found";
            return false;
        }

        uint16_t msgCtrl = Pci::LegacyRead16(bus, dev, func, cap + 2);
        bool is64bit = (msgCtrl & (1 << 7)) != 0;

        Pci::LegacyWrite32(bus, dev, func, cap + 4, MSI_ADDR_BASE);

        if (is64bit) {
            Pci::LegacyWrite32(bus, dev, func, cap + 8, 0);
            Pci::LegacyWrite16(bus, dev, func, cap + 12, MSI_VECTOR);
        } else {
            Pci::LegacyWrite16(bus, dev, func, cap + 8, MSI_VECTOR);
        }

        msgCtrl &= ~(0x70);  // Single message
        msgCtrl |= (1 << 0); // MSI Enable
        Pci::LegacyWrite16(bus, dev, func, cap + 2, msgCtrl);

        uint16_t pciCmd = Pci::LegacyRead16(bus, dev, func, (uint8_t)Pci::PCI_REG_COMMAND);
        pciCmd |= Pci::PCI_CMD_INTX_DISABLE;
        Pci::LegacyWrite16(bus, dev, func, (uint8_t)Pci::PCI_REG_COMMAND, pciCmd);

        Hal::RegisterIrqHandler(MSI_IRQ, HandleInterrupt);

        KernelLogStream(OK, "AHCI") << "MSI enabled: vector " << base::dec << (uint64_t)MSI_VECTOR;
        return true;
    }

    // -------------------------------------------------------------------------
    // HBA reset
    // -------------------------------------------------------------------------

    static bool ResetHba() {
        // Set AHCI Enable
        uint32_t ghc = ReadReg(REG_GHC);
        ghc |= GHC_AE;
        WriteReg(REG_GHC, ghc);

        // Perform HBA reset
        ghc = ReadReg(REG_GHC);
        ghc |= GHC_HR;
        WriteReg(REG_GHC, ghc);

        // Wait for reset to complete (HR should self-clear within 1 second)
        for (int i = 0; i < 1000000; i++) {
            if (!(ReadReg(REG_GHC) & GHC_HR)) {
                // Re-enable AHCI after reset
                ghc = ReadReg(REG_GHC);
                ghc |= GHC_AE;
                WriteReg(REG_GHC, ghc);
                return true;
            }
            asm volatile("" ::: "memory");
        }

        KernelLogStream(ERROR, "AHCI") << "HBA reset timed out";
        return false;
    }

    // -------------------------------------------------------------------------
    // Probe (PCI driver entry point)
    // -------------------------------------------------------------------------

    bool Probe(const Pci::PciDevice& dev) {
        if (g_initialized) return false;

        KernelLogStream(OK, "AHCI") << "Found AHCI controller at PCI "
            << base::hex << (uint64_t)dev.Bus << ":"
            << (uint64_t)dev.Device << "." << (uint64_t)dev.Function
            << " (" << (uint64_t)dev.VendorId << ":" << (uint64_t)dev.DeviceId << ")";

        // AHCI uses BAR5 (ABAR) for its MMIO registers.
        // BAR5 is at PCI config offset 0x24.
        uint32_t abarLow = Pci::LegacyRead32(dev.Bus, dev.Device, dev.Function, 0x24);
        uint64_t mmioPhys = abarLow & 0xFFFFFFF0u;

        // Check for 64-bit BAR
        if ((abarLow & 0x06) == 0x04) {
            uint32_t abarHigh = Pci::LegacyRead32(dev.Bus, dev.Device, dev.Function, 0x28);
            mmioPhys |= ((uint64_t)abarHigh << 32);
        }

        KernelLogStream(INFO, "AHCI") << "ABAR (BAR5) physical: " << base::hex << mmioPhys;

        // Map MMIO region (AHCI spec says up to 8 KiB for GHC + 32 ports)
        // Map 16 KiB to be safe
        constexpr uint64_t MmioSize = 0x4000;
        for (uint64_t offset = 0; offset < MmioSize; offset += 0x1000) {
            Memory::VMM::g_paging->MapMMIO(mmioPhys + offset, Memory::HHDM(mmioPhys + offset));
        }

        g_mmioBase = (volatile uint8_t*)Memory::HHDM(mmioPhys);

        // Enable bus mastering and memory space
        Pci::EnableBusMaster(dev.Bus, dev.Device, dev.Function);

        // BIOS/OS handoff
        PerformBiosHandoff();

        // Reset HBA
        if (!ResetHba()) {
            KernelLogStream(ERROR, "AHCI") << "HBA reset failed, aborting";
            return false;
        }

        // Read version
        uint32_t vs = ReadReg(REG_VS);
        uint32_t vsMajor = (vs >> 16) & 0xFFFF;
        uint32_t vsMinor = vs & 0xFFFF;
        KernelLogStream(INFO, "AHCI") << "Version: " << base::dec
            << (uint64_t)vsMajor << "." << (uint64_t)vsMinor;

        // Read capabilities
        uint32_t cap = ReadReg(REG_CAP);
        uint32_t numPorts = (cap & 0x1F) + 1;
        uint32_t numSlots = ((cap >> 8) & 0x1F) + 1;
        bool supports64bit = (cap & CAP_S64A) != 0;

        KernelLogStream(INFO, "AHCI") << "Ports: " << (uint64_t)numPorts
            << ", Command slots: " << (uint64_t)numSlots
            << ", 64-bit: " << (supports64bit ? "yes" : "no");

        // Read which ports are implemented
        g_portsImplemented = ReadReg(REG_PI);
        KernelLogStream(INFO, "AHCI") << "Ports implemented: " << base::hex << (uint64_t)g_portsImplemented;

        // Set up MSI (or fall back to legacy IRQ)
        bool hasMsi = SetupMsi(dev.Bus, dev.Device, dev.Function);

        if (!hasMsi) {
            uint8_t irqLine = Pci::LegacyRead8(dev.Bus, dev.Device, dev.Function,
                (uint8_t)Pci::PCI_REG_INTERRUPT);
            if (irqLine != 0xFF) {
                KernelLogStream(INFO, "AHCI") << "Using legacy IRQ " << base::dec << (uint64_t)irqLine;
                Hal::RegisterIrqHandler(irqLine, HandleInterrupt);
                Hal::IoApic::UnmaskIrq(Hal::IoApic::GetGsiForIrq(irqLine));
            }
        }

        // Enable global interrupts
        uint32_t ghc = ReadReg(REG_GHC);
        ghc |= GHC_IE;
        WriteReg(REG_GHC, ghc);

        // Initialize each implemented port
        g_activePortCount = 0;
        for (int i = 0; i < MAX_PORTS; i++) {
            if (!(g_portsImplemented & (1u << i))) continue;

            PortType type = ClassifyPort(i);
            g_ports[i].Type = type;

            if (type == PortType::None) continue;

            const char* typeStr = "Unknown";
            switch (type) {
                case PortType::Sata:           typeStr = "SATA"; break;
                case PortType::Satapi:         typeStr = "SATAPI"; break;
                case PortType::Semb:           typeStr = "SEMB"; break;
                case PortType::PortMultiplier: typeStr = "PM"; break;
                default: break;
            }

            KernelLogStream(INFO, "AHCI") << "Port " << base::dec << (uint64_t)i
                << ": " << typeStr << " device detected";

            InitPort(i);

            if (type == PortType::Sata) {
                if (IdentifyDevice(i)) {
                    g_ports[i].Active = true;
                    g_activePortCount++;

                    // Register as a block device
                    Storage::BlockDevice bdev = {};
                    bdev.ReadSectors = [](void* ctx, uint64_t lba, uint32_t count, void* buffer) -> bool {
                        return ReadSectors((int)(uintptr_t)ctx, lba, count, buffer);
                    };
                    bdev.WriteSectors = [](void* ctx, uint64_t lba, uint32_t count, const void* buffer) -> bool {
                        return WriteSectors((int)(uintptr_t)ctx, lba, count, buffer);
                    };
                    bdev.Ctx = (void*)(uintptr_t)i;
                    bdev.SectorCount = g_ports[i].SectorCount;
                    bdev.SectorSize = g_ports[i].SectorSizeLog;
                    memcpy(bdev.Model, g_ports[i].Model, 41);
                    Storage::RegisterBlockDevice(bdev);
                }
            }
        }

        g_initialized = true;

        KernelLogStream(OK, "AHCI") << "Initialization complete: "
            << base::dec << (uint64_t)g_activePortCount << " SATA device(s) ready";

        return true;
    }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    bool IsInitialized() {
        return g_initialized;
    }

    int GetPortCount() {
        return g_activePortCount;
    }

    const PortInfo* GetPortInfo(int port) {
        if (port < 0 || port >= MAX_PORTS || !g_ports[port].Active) {
            return nullptr;
        }
        return &g_ports[port];
    }

    uint64_t GetSectorCount(int port) {
        if (port < 0 || port >= MAX_PORTS || !g_ports[port].Active) {
            return 0;
        }
        return g_ports[port].SectorCount;
    }

    bool ReadSectors(int port, uint64_t lba, uint32_t count, void* buffer) {
        if (!g_initialized || port < 0 || port >= MAX_PORTS || !g_ports[port].Active) {
            return false;
        }
        if (count == 0 || buffer == nullptr) return false;

        // Limit to 128 sectors per command (64 KiB)
        if (count > 128) {
            KernelLogStream(ERROR, "AHCI") << "ReadSectors: count " << count << " exceeds max 128";
            return false;
        }

        int slot = FindFreeSlot(port);
        if (slot < 0) {
            KernelLogStream(ERROR, "AHCI") << "ReadSectors: no free command slot";
            return false;
        }

        // Allocate DMA buffer (we need physically contiguous memory for DMA)
        uint32_t totalBytes = count * SECTOR_SIZE;
        int pagesNeeded = (totalBytes + 0xFFF) / 0x1000;
        uint64_t dmaPhys;
        void* dmaVirt = AllocateDmaBuffer(dmaPhys, pagesNeeded);

        BuildReadWriteCommand(port, slot, lba, count, dmaPhys, false);

        bool ok = IssueCommand(port, slot);
        if (ok) {
            memcpy(buffer, dmaVirt, totalBytes);
        }

        Memory::g_pfa->Free(dmaVirt, pagesNeeded);
        return ok;
    }

    bool WriteSectors(int port, uint64_t lba, uint32_t count, const void* buffer) {
        if (!g_initialized || port < 0 || port >= MAX_PORTS || !g_ports[port].Active) {
            return false;
        }
        if (count == 0 || buffer == nullptr) return false;

        if (count > 128) {
            KernelLogStream(ERROR, "AHCI") << "WriteSectors: count " << count << " exceeds max 128";
            return false;
        }

        int slot = FindFreeSlot(port);
        if (slot < 0) {
            KernelLogStream(ERROR, "AHCI") << "WriteSectors: no free command slot";
            return false;
        }

        uint32_t totalBytes = count * SECTOR_SIZE;
        int pagesNeeded = (totalBytes + 0xFFF) / 0x1000;
        uint64_t dmaPhys;
        void* dmaVirt = AllocateDmaBuffer(dmaPhys, pagesNeeded);

        memcpy(dmaVirt, buffer, totalBytes);

        BuildReadWriteCommand(port, slot, lba, count, dmaPhys, true);

        bool ok = IssueCommand(port, slot);

        Memory::g_pfa->Free(dmaVirt, pagesNeeded);
        return ok;
    }

};
