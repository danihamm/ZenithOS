/*
    * FsProbe.hpp
    * Filesystem probe registry
    * Copyright (c) 2026 Daniel Hammer
*/

#pragma once
#include <Fs/Vfs.hpp>

namespace Fs::FsProbe {

    static constexpr int MaxProbes = 8;

    // A probe function attempts to mount a filesystem on the given partition.
    // Returns a FsDriver* on success, nullptr if the filesystem is not recognized.
    using ProbeFn = Vfs::FsDriver* (*)(int blockDevIndex, uint64_t startLba, uint64_t sectorCount);

    // Register a filesystem probe function (called once per FS driver at init).
    void Register(ProbeFn fn);

    // Probe all discovered GPT partitions and auto-mount recognized filesystems.
    // Assigns VFS drive numbers starting from firstDrive.
    void MountPartitions(int firstDrive = 1);

    // Try to mount a single partition (by global partition index) as the given VFS drive.
    // Returns 0 on success, -1 if no probe recognized the filesystem.
    int MountPartition(int partIndex, int driveNum);

};
