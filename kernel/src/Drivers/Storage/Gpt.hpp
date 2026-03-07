/*
    * Gpt.hpp
    * GUID Partition Table (GPT) parser
    * Copyright (c) 2026 Daniel Hammer
*/

#pragma once
#include <cstdint>

namespace Drivers::Storage::Gpt {

    // =========================================================================
    // On-disk structures (UEFI Specification §5)
    // =========================================================================

    struct Guid {
        uint32_t Data1;
        uint16_t Data2;
        uint16_t Data3;
        uint8_t  Data4[8];
    } __attribute__((packed));

    // Protective MBR partition entry (at LBA 0)
    struct MbrPartitionEntry {
        uint8_t  Status;
        uint8_t  ChsFirst[3];
        uint8_t  Type;
        uint8_t  ChsLast[3];
        uint32_t LbaFirst;
        uint32_t SectorCount;
    } __attribute__((packed));

    struct ProtectiveMbr {
        uint8_t           Bootstrap[446];
        MbrPartitionEntry Partitions[4];
        uint16_t          Signature;     // 0xAA55
    } __attribute__((packed));

    static_assert(sizeof(ProtectiveMbr) == 512);

    // GPT Header (at LBA 1, backup at last LBA)
    static constexpr uint64_t GPT_HEADER_SIGNATURE = 0x5452415020494645ULL; // "EFI PART"
    static constexpr uint32_t GPT_HEADER_REVISION  = 0x00010000;

    struct GptHeader {
        uint64_t Signature;
        uint32_t Revision;
        uint32_t HeaderSize;
        uint32_t HeaderCrc32;
        uint32_t Reserved;
        uint64_t MyLba;
        uint64_t AlternateLba;
        uint64_t FirstUsableLba;
        uint64_t LastUsableLba;
        Guid     DiskGuid;
        uint64_t PartitionEntryLba;
        uint32_t NumberOfPartitionEntries;
        uint32_t SizeOfPartitionEntry;
        uint32_t PartitionEntryArrayCrc32;
    } __attribute__((packed));

    static_assert(sizeof(GptHeader) == 92);

    // GPT Partition Entry (typically 128 bytes each)
    struct GptPartitionEntry {
        Guid     TypeGuid;
        Guid     UniqueGuid;
        uint64_t StartingLba;
        uint64_t EndingLba;
        uint64_t Attributes;
        uint16_t Name[36];    // UTF-16LE, 36 code units = 72 bytes
    } __attribute__((packed));

    static_assert(sizeof(GptPartitionEntry) == 128);

    // Partition attribute bits
    static constexpr uint64_t ATTR_PLATFORM_REQUIRED = (1ULL << 0);
    static constexpr uint64_t ATTR_EFI_IGNORE        = (1ULL << 1);
    static constexpr uint64_t ATTR_LEGACY_BIOS_BOOT  = (1ULL << 2);

    // Well-known partition type GUIDs
    static constexpr Guid GUID_UNUSED      = {0, 0, 0, {0,0,0,0,0,0,0,0}};
    // EFI System Partition: C12A7328-F81F-11D2-BA4B-00A0C93EC93B
    static constexpr Guid GUID_EFI_SYSTEM  = {0xC12A7328, 0xF81F, 0x11D2, {0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B}};
    // Microsoft Basic Data: EBD0A0A2-B9E5-4433-87C0-68B6B72699C7
    static constexpr Guid GUID_BASIC_DATA  = {0xEBD0A0A2, 0xB9E5, 0x4433, {0x87,0xC0,0x68,0xB6,0xB7,0x26,0x99,0xC7}};
    // Linux Filesystem: 0FC63DAF-8483-4772-8E79-3D69D8477DE4
    static constexpr Guid GUID_LINUX_FS    = {0x0FC63DAF, 0x8483, 0x4772, {0x8E,0x79,0x3D,0x69,0xD8,0x47,0x7D,0xE4}};
    // Linux Swap: 0657FD6D-A4AB-43C4-84E5-0933C84B4F4F
    static constexpr Guid GUID_LINUX_SWAP  = {0x0657FD6D, 0xA4AB, 0x43C4, {0x84,0xE5,0x09,0x33,0xC8,0x4B,0x4F,0x4F}};

    // =========================================================================
    // Parsed partition info (kernel-side)
    // =========================================================================

    static constexpr int MaxPartitions = 128;

    struct PartitionInfo {
        int      BlockDevIndex;   // which block device this partition lives on
        uint64_t StartLba;
        uint64_t EndLba;
        uint64_t SectorCount;
        Guid     TypeGuid;
        Guid     UniqueGuid;
        uint64_t Attributes;
        char     Name[72];        // ASCII-narrowed from UTF-16LE
    };

    // =========================================================================
    // Public API
    // =========================================================================

    // Probe a block device for a GPT. Returns the number of partitions found,
    // or 0 if no valid GPT was detected.
    int ProbeDevice(int blockDevIndex);

    // Probe all registered block devices for GPT.
    void ProbeAll();

    // Get total number of discovered partitions (across all devices).
    int GetPartitionCount();

    // Get partition info by global index.
    const PartitionInfo* GetPartition(int index);

    // Look up the human-readable name for a partition type GUID.
    const char* GetTypeName(const Guid& typeGuid);

    // =========================================================================
    // Write support — create GPT and add partitions
    // =========================================================================

    // Initialize a fresh GPT on a block device (destroys all existing data).
    // Creates protective MBR, primary + backup GPT headers, empty partition array.
    // Returns 0 on success, -1 on error.
    int InitializeGpt(int blockDevIndex);

    // Add a partition to an existing GPT.
    // startLba/endLba define the range (inclusive). Pass 0/0 to auto-fill
    // the largest contiguous free region.
    // typeGuid selects the partition type.
    // name is an ASCII label (up to 36 chars).
    // Returns the global partition index on success, -1 on error.
    int AddPartition(int blockDevIndex, uint64_t startLba, uint64_t endLba,
                     const Guid& typeGuid, const char* name);

};
