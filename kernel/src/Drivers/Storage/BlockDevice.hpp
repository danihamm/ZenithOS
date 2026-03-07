/*
    * BlockDevice.hpp
    * Driver-agnostic block device registry
    * Copyright (c) 2026 Daniel Hammer
*/

#pragma once
#include <cstdint>

namespace Drivers::Storage {

    static constexpr int MaxBlockDevices = 32;

    struct BlockDevice {
        bool (*ReadSectors)(void* ctx, uint64_t lba, uint32_t count, void* buffer);
        bool (*WriteSectors)(void* ctx, uint64_t lba, uint32_t count, const void* buffer);
        void*    Ctx;
        uint64_t SectorCount;
        uint16_t SectorSize;
        char     Model[41];
    };

    // Register a block device. Returns the assigned index, or -1 on failure.
    int RegisterBlockDevice(const BlockDevice& dev);

    // Get a registered block device by index. Returns nullptr if invalid.
    const BlockDevice* GetBlockDevice(int index);

    // Get the number of registered block devices.
    int GetBlockDeviceCount();

};
