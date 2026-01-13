# ZenithOS
A research project kernel and operating system in C++.

![Screenshot of ZenithOS](https://i.imgur.com/STTKTJG.png)

## Quickstart
To build the kernel, run `make` in the root directory. An ISO file will be generated in the root directory of this tree.

In order to run directly through QEMU, run `make run`.

## System architecture
ZenithOS is a 64-bit x86-64 kernel and operating system. It can run on both UEFI and legacy BIOS systems, but it is only tested on UEFI systems.

### Kernel code (src/) directory structure
| Dir. name | Description |
| --------- | ----------- |
| ACPI      | ACPI-related components |
| Common    | Kernel panic routines  |
| CppLib    | Kernel C++ library (inc. new/delete implementation, string functions, spinlock, vectors, string stream)
| Efi       | EFI-related definitions |
| Gui       | GUI components |
| Hal       | Low-level architecture code (interrupts, etc) |
| Io        | I/O port implementations
| Libraries | Shared string/memory routines + housing for external libraries |
| Memory    | Kernel memory components (inc. kernel heap, page frame allocation, virtual memory/paging)
| Platform  | Misc platform code
| Terminal  | Obsolete terminal implementation based on Flanterm
| Timekeeping | Time-related code