/*
    * main.cpp
    * Kernel entry point
    * Copyright (c) 2025 Daniel Hammer, Limine Contributors (via Limine C++ example)
*/

#include <cstdint>
#include <cstddef>
#include <limine.h>
#include <Hal/GDT.hpp>
#include <Gui/DebugGui.hpp>
#include <Gui/LogTable.hpp>
#include <Terminal/Terminal.hpp>
#include <Libraries/String.hpp>
#include <Efi/UEFI.hpp>
#include <Common/Panic.hpp>
#include <Memory/Memmap.hpp>
#include <Memory/Heap.hpp>
#include <Memory/HHDM.hpp>
#include <CppLib/Stream.hpp>
#include <CppLib/Vector.hpp>
#include <CppLib/CString.hpp>
#include <Platform/Limine.hpp>
#include <Platform/Util.hpp>
#include <Libraries/Memory.hpp>
#include <Hal/IDT.hpp>
#include <Memory/PageFrameAllocator.hpp>
#include <Memory/HHDM.hpp>
#include <Io/IoPort.hpp>
#include <Memory/Paging.hpp>
#include <ACPI/ACPI.hpp>
#include <CppLib/BoxUI.hpp>

namespace Memory {
    HeapAllocator* g_heap;
    PageFrameAllocator* g_pfa;
    uint64_t HHDMBase;
};

// Terminal streams (kept for panic/error handling fallback)
Kt::KernelOutStream kout{};
Kt::KernelErrorStream kerr{};

// Extern declarations for global constructors array.
extern void (*__init_array[])();
extern void (*__init_array_end[])();

extern "C" uint64_t KernelStartSymbol;
extern "C" uint64_t KernelEndSymbol;

extern "C" void kmain() {
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        Hal::Halt();
    }

    // Call global constructors.
    for (std::size_t i = 0; &__init_array[i] != __init_array_end; i++) {
        __init_array[i]();
    }

    if (framebuffer_request.response == nullptr
     || framebuffer_request.response->framebuffer_count < 1) {
        Hal::Halt();
    }
    
    limine_framebuffer *framebuffer{framebuffer_request.response->framebuffers[0]};

    // Initialize Flanterm for panic/error fallback (will be overwritten by GUI)
    Kt::Initialize(
        (uint32_t*)framebuffer->address,
        framebuffer->width,
        framebuffer->height,
        framebuffer->pitch,
        framebuffer->red_mask_size,
        framebuffer->red_mask_shift,
        framebuffer->green_mask_size,
        framebuffer->green_mask_shift,
        framebuffer->blue_mask_size,
        framebuffer->blue_mask_shift
    );

    // Initialize debug GUI (overwrites Flanterm display)
    Gui::Init(framebuffer);


#if defined (__x86_64__)
    Gui::Log(Gui::LogLevel::Info, "HAL", "Initializing GDT");
    Hal::PrepareGDT();
    Hal::BridgeLoadGDT();
    Gui::Log(Gui::LogLevel::Ok, "HAL", "GDT loaded");
#endif

    uint64_t hhdm_offset = hhdm_request.response->offset;
    Memory::HHDMBase = hhdm_offset;

    if (memmap_request.response != nullptr) {
        Gui::Log(Gui::LogLevel::Ok, "Memory", "Creating PageFrameAllocator");

        Memory::PageFrameAllocator pmm(Memory::Scan(memmap_request.response));
        Memory::g_pfa = &pmm;

        Gui::Log(Gui::LogLevel::Ok, "Memory", "Creating HeapAllocator");
        Memory::HeapAllocator heap{};
        Memory::g_heap = &heap;

        Gui::Log(Gui::LogLevel::Ok, "Memory", "HeapAllocator initialized");
    } else {
        Gui::Log(Gui::LogLevel::Error, "Boot", "System memory map missing!");
        Hal::Halt();
    }


#if defined (__x86_64__)
    Gui::Log(Gui::LogLevel::Info, "HAL", "Initializing IDT");
    Hal::IDTInitialize();
    Gui::Log(Gui::LogLevel::Ok, "HAL", "IDT loaded");

    Gui::Log(Gui::LogLevel::Info, "Memory", "Initializing paging");
    Memory::VMM::Paging g_paging{};
    g_paging.Init((uint64_t)&KernelStartSymbol, ((uint64_t)&KernelEndSymbol - (uint64_t)&KernelStartSymbol), memmap_request.response);
    Gui::Log(Gui::LogLevel::Ok, "Memory", "Paging initialized");
#endif

    Gui::Log(Gui::LogLevel::Info, "ACPI", "Parsing ACPI tables");
    Hal::ACPI g_acpi((Hal::ACPI::XSDP*)Memory::HHDM(rsdp_request.response->address));
    Gui::Log(Gui::LogLevel::Ok, "ACPI", "ACPI tables parsed");

    Gui::Log(Gui::LogLevel::Info, "UEFI", "Initializing UEFI runtime");
    Efi::SystemTable* ST = (Efi::SystemTable*)Memory::HHDM(system_table_request.response->address);
    Efi::Init(ST);
    Gui::Log(Gui::LogLevel::Ok, "UEFI", "UEFI runtime initialized");

    Gui::Log(Gui::LogLevel::Ok, "Boot", "Kernel initialization complete");

    Hal::Halt();
}
