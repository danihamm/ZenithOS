/*
    * Vfs.cpp
    * Virtual File System with numerical logical drive identifiers
    * Copyright (c) 2025 Daniel Hammer
*/

#include "Vfs.hpp"
#include <Terminal/Terminal.hpp>
#include <CppLib/Spinlock.hpp>

namespace Fs::Vfs {

    struct HandleEntry {
        bool inUse;
        int driveNumber;
        int localHandle;
    };

    static FsDriver* driveTable[MaxDrives];
    static HandleEntry handleTable[MaxHandles];

    // Protects handle table and driver dispatch from concurrent CPU access.
    // Uses Mutex (not Spinlock) so interrupts stay enabled while held --
    // VFS is never called from interrupt context.
    static kcp::Mutex vfsLock;

    // Parse "N:/path" into drive number and local path.
    // Returns true on success, sets outDrive and outPath.
    static bool ParsePath(const char* path, int& outDrive, const char*& outPath) {
        if (path == nullptr) return false;

        // Parse decimal drive number before ':'
        int drive = 0;
        int i = 0;
        bool hasDigit = false;

        while (path[i] >= '0' && path[i] <= '9') {
            drive = drive * 10 + (path[i] - '0');
            hasDigit = true;
            i++;
        }

        if (!hasDigit) return false;
        if (path[i] != ':') return false;

        // Everything after "N:" is the local path
        outDrive = drive;
        outPath = &path[i + 1];
        return true;
    }

    static int AllocHandle() {
        for (int i = 0; i < MaxHandles; i++) {
            if (!handleTable[i].inUse) return i;
        }
        return -1;
    }

    void Initialize() {
        for (int i = 0; i < MaxDrives; i++) {
            driveTable[i] = nullptr;
        }
        for (int i = 0; i < MaxHandles; i++) {
            handleTable[i].inUse = false;
        }

        Kt::KernelLogStream(Kt::OK, "VFS") << "Initialized (" << MaxDrives << " drives, " << MaxHandles << " handles)";
    }

    int RegisterDrive(int driveNumber, FsDriver* driver) {
        if (driveNumber < 0 || driveNumber >= MaxDrives) return -1;
        if (driver == nullptr) return -1;

        driveTable[driveNumber] = driver;
        Kt::KernelLogStream(Kt::OK, "VFS") << "Registered drive " << driveNumber;
        return 0;
    }

    int VfsOpen(const char* path) {
        int drive;
        const char* localPath;

        if (!ParsePath(path, drive, localPath)) return -1;
        if (drive < 0 || drive >= MaxDrives || driveTable[drive] == nullptr) return -1;

        vfsLock.Acquire();

        int localHandle = driveTable[drive]->Open(localPath);
        if (localHandle < 0) { vfsLock.Release(); return -1; }

        int globalHandle = AllocHandle();
        if (globalHandle < 0) {
            driveTable[drive]->Close(localHandle);
            vfsLock.Release();
            return -1;
        }

        handleTable[globalHandle].inUse = true;
        handleTable[globalHandle].driveNumber = drive;
        handleTable[globalHandle].localHandle = localHandle;

        vfsLock.Release();
        return globalHandle;
    }

    int VfsRead(int handle, uint8_t* buffer, uint64_t offset, uint64_t size) {
        vfsLock.Acquire();
        if (handle < 0 || handle >= MaxHandles || !handleTable[handle].inUse) { vfsLock.Release(); return -1; }

        HandleEntry& entry = handleTable[handle];
        int result = driveTable[entry.driveNumber]->Read(entry.localHandle, buffer, offset, size);
        vfsLock.Release();
        return result;
    }

    uint64_t VfsGetSize(int handle) {
        vfsLock.Acquire();
        if (handle < 0 || handle >= MaxHandles || !handleTable[handle].inUse) { vfsLock.Release(); return 0; }

        HandleEntry& entry = handleTable[handle];
        uint64_t result = driveTable[entry.driveNumber]->GetSize(entry.localHandle);
        vfsLock.Release();
        return result;
    }

    void VfsClose(int handle) {
        vfsLock.Acquire();
        if (handle < 0 || handle >= MaxHandles || !handleTable[handle].inUse) { vfsLock.Release(); return; }

        HandleEntry& entry = handleTable[handle];
        driveTable[entry.driveNumber]->Close(entry.localHandle);
        entry.inUse = false;
        vfsLock.Release();
    }

    int VfsWrite(int handle, const uint8_t* buffer, uint64_t offset, uint64_t size) {
        vfsLock.Acquire();
        if (handle < 0 || handle >= MaxHandles || !handleTable[handle].inUse) { vfsLock.Release(); return -1; }

        HandleEntry& entry = handleTable[handle];
        if (driveTable[entry.driveNumber]->Write == nullptr) { vfsLock.Release(); return -1; }
        int result = driveTable[entry.driveNumber]->Write(entry.localHandle, buffer, offset, size);
        vfsLock.Release();
        return result;
    }

    int VfsCreate(const char* path) {
        int drive;
        const char* localPath;

        if (!ParsePath(path, drive, localPath)) return -1;
        if (drive < 0 || drive >= MaxDrives || driveTable[drive] == nullptr) return -1;
        if (driveTable[drive]->Create == nullptr) return -1;

        vfsLock.Acquire();

        int localHandle = driveTable[drive]->Create(localPath);
        if (localHandle < 0) { vfsLock.Release(); return -1; }

        int globalHandle = AllocHandle();
        if (globalHandle < 0) {
            vfsLock.Release();
            return -1;
        }

        handleTable[globalHandle].inUse = true;
        handleTable[globalHandle].driveNumber = drive;
        handleTable[globalHandle].localHandle = localHandle;

        vfsLock.Release();
        return globalHandle;
    }

    int VfsDelete(const char* path) {
        int drive;
        const char* localPath;

        if (!ParsePath(path, drive, localPath)) return -1;
        if (drive < 0 || drive >= MaxDrives || driveTable[drive] == nullptr) return -1;
        if (driveTable[drive]->Delete == nullptr) return -1;

        vfsLock.Acquire();
        int result = driveTable[drive]->Delete(localPath);
        vfsLock.Release();
        return result;
    }

    int VfsMkdir(const char* path) {
        int drive;
        const char* localPath;

        if (!ParsePath(path, drive, localPath)) return -1;
        if (drive < 0 || drive >= MaxDrives || driveTable[drive] == nullptr) return -1;
        if (driveTable[drive]->Mkdir == nullptr) return -1;

        vfsLock.Acquire();
        int result = driveTable[drive]->Mkdir(localPath);
        vfsLock.Release();
        return result;
    }

    int VfsDriveList(int* outDrives, int maxEntries) {
        int count = 0;
        for (int i = 0; i < MaxDrives && count < maxEntries; i++) {
            if (driveTable[i] != nullptr) {
                outDrives[count++] = i;
            }
        }
        return count;
    }

    int VfsReadDir(const char* path, const char** outNames, int maxEntries) {
        int drive;
        const char* localPath;

        if (!ParsePath(path, drive, localPath)) return -1;
        if (drive < 0 || drive >= MaxDrives || driveTable[drive] == nullptr) return -1;

        vfsLock.Acquire();
        int result = driveTable[drive]->ReadDir(localPath, outNames, maxEntries);
        vfsLock.Release();
        return result;
    }

}
