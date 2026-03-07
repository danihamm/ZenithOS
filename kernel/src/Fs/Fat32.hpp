/*
    * Fat32.hpp
    * FAT32 filesystem driver
    * Copyright (c) 2026 Daniel Hammer
*/

#pragma once
#include <cstdint>
#include <Fs/Vfs.hpp>

namespace Fs::Fat32 {

    // Try to mount a FAT32 filesystem at the given partition range.
    // Returns a FsDriver* on success, nullptr if not a valid FAT32 volume.
    Vfs::FsDriver* Mount(int blockDevIndex, uint64_t startLba, uint64_t sectorCount);

    // Register Fat32::Mount as a filesystem probe with FsProbe.
    void RegisterProbe();

    // Format a partition as FAT32.
    // blockDevIndex: which block device the partition is on
    // startLba: first LBA of the partition
    // sectorCount: number of sectors in the partition
    // volumeLabel: up to 11-char volume label (null-terminated)
    // Returns 0 on success, -1 on error.
    int Format(int blockDevIndex, uint64_t startLba, uint64_t sectorCount,
               const char* volumeLabel);

};
