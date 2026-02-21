# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
make                  # Build full ISO (kernel + programs + ramdisk)
make kernel           # Build kernel only
make -C programs      # Build all userspace programs only
make run              # Build and run in QEMU (requires sudo for tap networking)
make run-bios         # Run in QEMU with legacy BIOS boot
make clean            # Remove build artifacts (preserves toolchain/limine)
make distclean        # Full clean including downloaded dependencies
make toolchain        # Build the cross-compilation toolchain
```

Building individual components:
```bash
make -C programs/src/desktop    # Rebuild desktop environment
make -C programs/src/wikipedia  # Rebuild Wikipedia standalone client
make -C programs/src/fetch      # Rebuild fetch (HTTP client)
make -C programs/src/doom       # Rebuild DOOM port
make -C programs/lib/bearssl    # Rebuild BearSSL library
make -C programs/lib/libc       # Rebuild minimal libc
```

Simple programs (single `main.cpp`) are auto-discovered and built by `programs/GNUmakefile`. Programs with custom build needs (desktop, doom, fetch, wiki, wikipedia) have their own Makefiles in their source directories.

## Build Pipeline

`make` → kernel binary + all programs → `scripts/mkramdisk.sh` tars `programs/bin/` → xorriso assembles ISO with Limine bootloader. The ramdisk (USTAR tar) becomes the root filesystem at boot, mounted as drive `0:`.

## Architecture Overview

**Kernel** (`kernel/src/`): Preemptive multitasking x86_64 kernel booted via Limine. C++20, freestanding, loaded at `0xffffffff80000000`. Key subsystems:
- `Api/` — Syscall dispatch (`Syscall.cpp`) and Window Server (`WinServer.cpp`)
- `Sched/` — Round-robin scheduler, 10ms time slices, max 16 processes
- `Memory/` — Physical page allocator, paging (per-process PML4), HHDM
- `Net/` — Full TCP/IP stack, sockets, DNS resolver
- `Drivers/` — E1000 NIC, Intel GPU, XHCI USB, PS/2 keyboard/mouse
- `Fs/` — VFS layer with ramdisk driver (reads tar archive)

**Userspace** (`programs/`): Programs loaded at `0x400000` (see `link.ld`). Entry point is `extern "C" void _start()`. No libc by default — syscalls via `<zenith/syscall.h>` wrappers. Programs needing libc/BearSSL link against `lib/libc` and `lib/bearssl`.

## Dual Syscall Headers

Syscall numbers are defined in **two mirrored copies** of `Api/Syscall.hpp`:
- `kernel/src/Api/Syscall.hpp` — used by kernel
- `programs/include/Api/Syscall.hpp` — used by userspace

**Both must be kept in sync** when adding or modifying syscalls. The userspace copy also contains shared struct definitions (`WinEvent`, `WinInfo`, `KeyEvent`, `DateTime`, `NetCfg`, etc.) used by both sides.

Userspace wrappers live in `programs/include/zenith/syscall.h` (inline functions using `syscall1`..`syscall6` asm helpers).

## GUI: Two Application Models

**Embedded apps** (compiled into `desktop.elf`): Source in `programs/src/desktop/app_*.cpp`. Render via callbacks (`on_draw`, `on_mouse`, `on_key`). The desktop allocates and manages their pixel buffers. These share a single process.

**External Window Server apps** (separate ELF processes, e.g. Wikipedia): Create a window via `win_create()` syscall, get a shared-memory pixel buffer, render directly, call `win_present()`. The desktop discovers them via `win_enumerate()`, maps their buffers with `win_map()`, and composites them. Events are forwarded via `win_sendevent()`. The kernel's `WinServer` (`kernel/src/Api/WinServer.cpp`) manages the shared-memory slots.

## Key Conventions

- **Compiler**: `x86_64-elf-g++` from `toolchain/local/` (auto-detected, falls back to system g++)
- **Standards**: GNU++20 (C++20 with extensions), freestanding, no exceptions, no RTTI
- **VFS paths**: `driveNum:/path` format, e.g. `0:/os/shell.elf`, `0:/fonts/Roboto-Medium.ttf`
- **Fonts**: TTF files in `programs/gui/fonts/`, copied to `programs/bin/fonts/` by `scripts/copy_fonts.sh`
- **Icons**: SVG files in `programs/gui/icons/` (Flat-Remix set), copied by `scripts/copy_icons.sh`
- **No test framework**: Validation is done by booting in QEMU (`make run`)

## Adding a New Syscall

1. Pick next available number in both `kernel/src/Api/Syscall.hpp` and `programs/include/Api/Syscall.hpp`
2. Implement `static ... Sys_Foo(...)` in `kernel/src/Api/Syscall.cpp`
3. Add `case SYS_FOO:` to the dispatch switch in `SyscallDispatch`
4. Add inline wrapper in `programs/include/zenith/syscall.h`
