/*
    * ElfLoader.hpp
    * ELF64 binary loader for user-mode processes
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <cstdint>

namespace Sched {

    struct Elf64Header {
        uint8_t  e_ident[16];
        uint16_t e_type;
        uint16_t e_machine;
        uint32_t e_version;
        uint64_t e_entry;
        uint64_t e_phoff;
        uint64_t e_shoff;
        uint32_t e_flags;
        uint16_t e_ehsize;
        uint16_t e_phentsize;
        uint16_t e_phnum;
        uint16_t e_shentsize;
        uint16_t e_shnum;
        uint16_t e_shstrndx;
    };

    struct Elf64ProgramHeader {
        uint32_t p_type;
        uint32_t p_flags;
        uint64_t p_offset;
        uint64_t p_vaddr;
        uint64_t p_paddr;
        uint64_t p_filesz;
        uint64_t p_memsz;
        uint64_t p_align;
    };

    static constexpr uint32_t PT_LOAD    = 1;
    static constexpr uint16_t ET_EXEC    = 2;
    static constexpr uint16_t EM_X86_64  = 62;

    // Load an ELF64 binary into a per-process address space.
    // pml4Phys = physical address of the process's PML4.
    // Returns the entry point address, or 0 on failure.
    uint64_t ElfLoad(const char* vfsPath, uint64_t pml4Phys);

}
