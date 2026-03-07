/*
    * FsProbe.cpp
    * Filesystem probe registry
    * Copyright (c) 2026 Daniel Hammer
*/

#include "FsProbe.hpp"
#include <Drivers/Storage/Gpt.hpp>
#include <Terminal/Terminal.hpp>

namespace Fs::FsProbe {

    static ProbeFn g_probes[MaxProbes] = {};
    static int g_probeCount = 0;

    void Register(ProbeFn fn) {
        if (g_probeCount < MaxProbes && fn) {
            g_probes[g_probeCount++] = fn;
        }
    }

    void MountPartitions(int firstDrive) {
        int partCount = Drivers::Storage::Gpt::GetPartitionCount();
        if (partCount == 0 || g_probeCount == 0) return;

        int driveNum = firstDrive;

        for (int i = 0; i < partCount && driveNum < Vfs::MaxDrives; i++) {
            auto* part = Drivers::Storage::Gpt::GetPartition(i);
            if (!part) continue;

            for (int p = 0; p < g_probeCount; p++) {
                Vfs::FsDriver* driver = g_probes[p](
                    part->BlockDevIndex, part->StartLba, part->SectorCount);

                if (driver) {
                    Vfs::RegisterDrive(driveNum, driver);
                    Kt::KernelLogStream(Kt::OK, "FsProbe") << "Mounted partition "
                        << i << " as drive " << driveNum;
                    driveNum++;
                    break;
                }
            }
        }
    }

    int MountPartition(int partIndex, int driveNum) {
        if (driveNum < 0 || driveNum >= Vfs::MaxDrives) return -1;
        if (g_probeCount == 0) return -1;

        auto* part = Drivers::Storage::Gpt::GetPartition(partIndex);
        if (!part) return -1;

        for (int p = 0; p < g_probeCount; p++) {
            Vfs::FsDriver* driver = g_probes[p](
                part->BlockDevIndex, part->StartLba, part->SectorCount);

            if (driver) {
                Vfs::RegisterDrive(driveNum, driver);
                Kt::KernelLogStream(Kt::OK, "FsProbe") << "Mounted partition "
                    << partIndex << " as drive " << driveNum;
                return 0;
            }
        }

        Kt::KernelLogStream(Kt::WARNING, "FsProbe") << "No filesystem recognized on partition " << partIndex;
        return -1;
    }

};
