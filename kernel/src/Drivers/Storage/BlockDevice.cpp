/*
    * BlockDevice.cpp
    * Driver-agnostic block device registry
    * Copyright (c) 2026 Daniel Hammer
*/

#include "BlockDevice.hpp"

namespace Drivers::Storage {

    static BlockDevice g_devices[MaxBlockDevices] = {};
    static int g_deviceCount = 0;

    int RegisterBlockDevice(const BlockDevice& dev) {
        if (g_deviceCount >= MaxBlockDevices) return -1;
        g_devices[g_deviceCount] = dev;
        return g_deviceCount++;
    }

    const BlockDevice* GetBlockDevice(int index) {
        if (index < 0 || index >= g_deviceCount) return nullptr;
        return &g_devices[index];
    }

    int GetBlockDeviceCount() {
        return g_deviceCount;
    }

};
