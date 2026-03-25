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

// ---- Search and execute an external command ----

int exec_external(const char* cmd, const char* args) {
    char path[256];
    const char* finalArgs = (args && args[0]) ? args : nullptr;

    // Strip leading "./" from command name
    const char* name = cmd;
    if (name[0] == '.' && name[1] == '/') name += 2;

    // 0. Direct paths are resolved by the kernel against the process CWD.
    bool hasSlash = false;
    for (int i = 0; cmd[i]; i++) {
        if (cmd[i] == '/') {
            hasSlash = true;
            break;
        }
    }
    if (has_drive_prefix(cmd) || cmd[0] == '/' || cmd[0] == '.' || hasSlash) {
        if (try_exec(cmd, finalArgs)) return 0;
        scopy(path, cmd, sizeof(path));
        scat(path, ".elf", sizeof(path));
        if (try_exec(path, finalArgs)) return 0;
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
