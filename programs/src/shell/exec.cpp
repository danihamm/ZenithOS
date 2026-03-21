/*
    * exec.cpp
    * External command search and execution
    * Copyright (c) 2025-2026 Daniel Hammer
*/

#include "shell.h"

// ---- Check if a file exists ----

static bool file_exists(const char* path) {
    int h = montauk::open(path);
    if (h < 0) return false;
    montauk::close(h);
    return true;
}

// ---- Try to spawn an ELF at the given path ----

static bool try_exec(const char* path, const char* args) {
    if (!file_exists(path)) return false;
    int pid = montauk::spawn(path, args);
    if (pid < 0) return false;
    montauk::waitpid(pid);
    return true;
}

// ---- Build an absolute path from CWD + relative name ----

static void build_cwd_path(const char* name, char* out, int outMax) {
    build_drive_path(current_drive, "", out, outMax);
    if (cwd[0]) {
        scat(out, cwd, outMax);
        scat(out, "/", outMax);
    }
    scat(out, name, outMax);
}

// ---- Resolve arguments: expand relative file paths against CWD ----

static void resolve_args(const char* args, char* out, int outMax) {
    if (!args || !args[0]) { out[0] = '\0'; return; }

    int o = 0;
    const char* p = args;

    while (*p && o < outMax - 1) {
        while (*p == ' ' && o < outMax - 1) { out[o++] = *p++; }
        if (!*p) break;

        const char* tokStart = p;
        int tokLen = 0;
        while (p[tokLen] && p[tokLen] != ' ') tokLen++;

        bool resolve = (cwd[0] || current_drive != 0) && !has_drive_prefix(tokStart) && tokStart[0] != '-';

        if (resolve) {
            char candidate[256];
            int r = 0;
            if (current_drive >= 10) candidate[r++] = '0' + current_drive / 10;
            candidate[r++] = '0' + current_drive % 10;
            candidate[r++] = ':'; candidate[r++] = '/';
            int j = 0;
            while (cwd[j] && r < 255) candidate[r++] = cwd[j++];
            if (cwd[0] && r < 255) candidate[r++] = '/';

            // Strip leading "./" from token
            int skip = 0;
            if (tokLen >= 2 && tokStart[0] == '.' && tokStart[1] == '/') skip = 2;

            for (int k = skip; k < tokLen && r < 255; k++) candidate[r++] = tokStart[k];
            candidate[r] = '\0';

            int h = montauk::open(candidate);
            if (h >= 0) {
                montauk::close(h);
                for (int k = 0; k < r && o < outMax - 1; k++) out[o++] = candidate[k];
            } else {
                bool looksLikeFile = false;
                for (int k = 0; k < tokLen; k++) {
                    if (tokStart[k] == '.') { looksLikeFile = true; break; }
                }
                if (looksLikeFile) {
                    for (int k = 0; k < r && o < outMax - 1; k++) out[o++] = candidate[k];
                } else {
                    for (int k = 0; k < tokLen && o < outMax - 1; k++) out[o++] = tokStart[k];
                }
            }
        } else {
            for (int k = 0; k < tokLen && o < outMax - 1; k++) out[o++] = tokStart[k];
        }

        p = tokStart + tokLen;
    }
    out[o] = '\0';
}

// ---- Search and execute an external command ----

int exec_external(const char* cmd, const char* args) {
    char path[256];

    char resolvedArgs[512];
    resolve_args(args, resolvedArgs, sizeof(resolvedArgs));
    const char* finalArgs = resolvedArgs[0] ? resolvedArgs : nullptr;

    // Strip leading "./" from command name
    const char* name = cmd;
    if (name[0] == '.' && name[1] == '/') name += 2;

    // 0. If cmd has a drive prefix (absolute path), try it directly
    if (has_drive_prefix(cmd)) {
        if (try_exec(cmd, finalArgs)) return 0;
        montauk::print(cmd);
        montauk::print(": not found\n");
        return 127;
    }

    // 1. Try as-is in CWD (exact name, e.g., "a.out" or "hello.elf")
    build_cwd_path(name, path, sizeof(path));
    if (try_exec(path, finalArgs)) return 0;

    // 2. Try with .elf extension in CWD
    build_cwd_path(name, path, sizeof(path));
    scat(path, ".elf", sizeof(path));
    if (try_exec(path, finalArgs)) return 0;

    // 3. Try 0:/os/<cmd>.elf
    scopy(path, "0:/os/", sizeof(path));
    scat(path, name, sizeof(path));
    scat(path, ".elf", sizeof(path));
    if (try_exec(path, finalArgs)) return 0;

    // 4. Try 0:/os/<cmd> (no extension)
    scopy(path, "0:/os/", sizeof(path));
    scat(path, name, sizeof(path));
    if (try_exec(path, finalArgs)) return 0;

    // 5. Try 0:/games/<cmd>.elf
    scopy(path, "0:/games/", sizeof(path));
    scat(path, name, sizeof(path));
    scat(path, ".elf", sizeof(path));
    if (try_exec(path, finalArgs)) return 0;

    // 6. If on a non-zero drive, also try drive root
    if (current_drive != 0) {
        build_drive_path(current_drive, name, path, sizeof(path));
        if (try_exec(path, finalArgs)) return 0;

        build_drive_path(current_drive, name, path, sizeof(path));
        scat(path, ".elf", sizeof(path));
        if (try_exec(path, finalArgs)) return 0;
    }

    montauk::print(cmd);
    montauk::print(": command not found\n");
    return 127;
}
