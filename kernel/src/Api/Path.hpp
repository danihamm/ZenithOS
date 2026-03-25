/*
    * Path.hpp
    * Per-process working-directory path resolution helpers
    * Copyright (c) 2026 Daniel Hammer
*/

#pragma once
#include <cstddef>
#include <Sched/Scheduler.hpp>
#include <Libraries/Memory.hpp>

namespace Montauk {

    static int ParseDrivePrefix(const char* path, int* outPrefixLen = nullptr) {
        if (path == nullptr || path[0] < '0' || path[0] > '9') return -1;

        int drive = 0;
        int i = 0;
        while (path[i] >= '0' && path[i] <= '9') {
            drive = drive * 10 + (path[i] - '0');
            i++;
        }

        if (path[i] != ':') return -1;
        if (outPrefixLen) *outPrefixLen = i + 1;
        return drive;
    }

    static int WriteDriveRoot(char* out, int outMax, int drive) {
        if (out == nullptr || outMax < 4 || drive < 0) return -1;

        char digits[12];
        int digitCount = 0;
        do {
            digits[digitCount++] = (char)('0' + (drive % 10));
            drive /= 10;
        } while (drive > 0 && digitCount < (int)sizeof(digits));

        if (digitCount + 2 >= outMax) return -1;

        int len = 0;
        for (int i = digitCount - 1; i >= 0; i--) out[len++] = digits[i];
        out[len++] = ':';
        out[len++] = '/';
        out[len] = '\0';
        return len;
    }

    static bool AppendPathSegment(char* out, int outMax, int& len, int rootLen,
                                  int* segmentBases, int& segmentCount,
                                  const char* segment, int segmentLen) {
        if (segmentLen <= 0) return true;
        if (segmentLen == 1 && segment[0] == '.') return true;

        if (segmentLen == 2 && segment[0] == '.' && segment[1] == '.') {
            if (segmentCount > 0) {
                len = segmentBases[--segmentCount];
                out[len] = '\0';
            }
            return true;
        }

        if (segmentCount >= 64) return false;

        int baseLen = len;
        if (len > rootLen) {
            if (len >= outMax - 1) return false;
            out[len++] = '/';
        }

        if (len + segmentLen >= outMax) return false;
        memcpy(out + len, segment, (std::size_t)segmentLen);
        len += segmentLen;
        out[len] = '\0';
        segmentBases[segmentCount++] = baseLen;
        return true;
    }

    static bool NormalizePathInto(char* out, int outMax, int& len, int rootLen,
                                  int* segmentBases, int& segmentCount,
                                  const char* path) {
        const char* segmentStart = path;
        for (const char* p = path;; ++p) {
            if (*p == '/' || *p == '\0') {
                if (!AppendPathSegment(out, outMax, len, rootLen, segmentBases,
                                       segmentCount, segmentStart, (int)(p - segmentStart))) {
                    return false;
                }
                if (*p == '\0') break;
                segmentStart = p + 1;
            }
        }
        return true;
    }

    static bool ResolvePathAgainst(const char* cwd, const char* path, char* out, int outMax) {
        if (cwd == nullptr || path == nullptr || out == nullptr || outMax < 4 || path[0] == '\0') return false;

        int drive = -1;
        int inputPrefixLen = 0;
        const char* remainder = path;
        bool useCwdBase = false;

        drive = ParseDrivePrefix(path, &inputPrefixLen);
        if (drive >= 0) {
            remainder = path + inputPrefixLen;
            if (*remainder == '/') remainder++;
        } else if (path[0] == '/') {
            drive = ParseDrivePrefix(cwd);
            if (drive < 0) return false;
            remainder = path + 1;
        } else {
            drive = ParseDrivePrefix(cwd);
            if (drive < 0) return false;
            useCwdBase = true;
        }

        int segmentBases[64];
        int segmentCount = 0;
        int len = WriteDriveRoot(out, outMax, drive);
        if (len < 0) return false;
        int rootLen = len;

        if (useCwdBase) {
            int cwdPrefixLen = 0;
            if (ParseDrivePrefix(cwd, &cwdPrefixLen) < 0) return false;

            const char* cwdRemainder = cwd + cwdPrefixLen;
            if (*cwdRemainder == '/') cwdRemainder++;
            if (!NormalizePathInto(out, outMax, len, rootLen, segmentBases, segmentCount, cwdRemainder)) {
                return false;
            }
        }

        return NormalizePathInto(out, outMax, len, rootLen, segmentBases, segmentCount, remainder);
    }

    static bool ResolveProcessPath(const char* path, char* out, int outMax) {
        auto* proc = Sched::GetCurrentProcessPtr();
        const char* cwd = (proc && proc->cwd[0]) ? proc->cwd : "0:/";
        return ResolvePathAgainst(cwd, path, out, outMax);
    }
}
