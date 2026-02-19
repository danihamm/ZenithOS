/*
    * Panic.cpp
    * Copyright (c) 2025 Daniel Hammer
*/

#include "Panic.hpp"
#include "../CppLib/BoxUI.hpp"

void Panic(const char *meditationString, System::PanicFrame* frame) {
    const int boxWidth = 72;

    // Header
    kerr << BOXUI_ANSI_RED_BG << BOXUI_ANSI_WHITE_FG << BOXUI_ANSI_BOLD << "\n";
    kerr << BOXUI_TL;
    for (int i = 0; i < boxWidth - 2; ++i) kerr << BOXUI_H;
    kerr << BOXUI_TR << "\n";
    PrintBoxedLine(kerr, "!!! KERNEL PANIC !!!", boxWidth, true);
    PrintBoxedLine(kerr, "", boxWidth);
    PrintBoxedLine(kerr, "System halted. Please reboot.", boxWidth, true);
    PrintBoxedLine(kerr, "", boxWidth);
    PrintBoxedSeparator(kerr, boxWidth);
    PrintBoxedLine(kerr, "Meditation:", boxWidth, true);
    PrintBoxedLine(kerr, meditationString, boxWidth);
    PrintBoxedLine(kerr, "", boxWidth);

#if defined (__x86_64__)
    if (frame != nullptr) {
        PrintBoxedSeparator(kerr, boxWidth);
        PrintBoxedLine(kerr, "CPU State:", boxWidth, true);
        PrintBoxedHex(kerr, "Interrupt Vector", frame->InterruptVector, boxWidth);

        if (frame->InterruptVector == 0xE) {
            auto pf_frame = (System::PageFaultPanicFrame*)frame;
            frame = (System::PanicFrame*)&pf_frame->IP;

            // CR2 holds the faulting virtual address for page faults
            uint64_t cr2;
            asm volatile("mov %%cr2, %0" : "=r"(cr2));
            PrintBoxedHex(kerr, "Faulting Address (CR2)", cr2, boxWidth);

            PrintBoxedLine(kerr, "Page Fault Error:", boxWidth, true);
            PrintBoxedDec(kerr, "Present", pf_frame->PageFaultError.Present, boxWidth);
            PrintBoxedDec(kerr, "Write", pf_frame->PageFaultError.Write, boxWidth);
            PrintBoxedDec(kerr, "User", pf_frame->PageFaultError.User, boxWidth);
            PrintBoxedDec(kerr, "Reserved Write", pf_frame->PageFaultError.ReservedWrite, boxWidth);
            PrintBoxedDec(kerr, "Instruction Fetch", pf_frame->PageFaultError.InstructionFetch, boxWidth);
            PrintBoxedDec(kerr, "Protection Key", pf_frame->PageFaultError.ProtectionKey, boxWidth);
            PrintBoxedDec(kerr, "Shadow Stack", pf_frame->PageFaultError.ShadowStack, boxWidth);
            PrintBoxedDec(kerr, "SGX", pf_frame->PageFaultError.SGX, boxWidth);
        } else if (frame->InterruptVector == 0xD) {
            auto gpf_frame = (System::GPFPanicFrame*)frame;
            frame = (System::PanicFrame*)&frame->IP;
            PrintBoxedLine(kerr, "General Protection Fault:", boxWidth, true);
            PrintBoxedDec(kerr, "Error Code", gpf_frame->GeneralProtectionFaultError, boxWidth);
        }

        PrintBoxedSeparator(kerr, boxWidth);
        PrintBoxedLine(kerr, "Registers:", boxWidth, true);
        PrintBoxedHex(kerr, "Instruction Pointer", frame->IP, boxWidth);
        PrintBoxedHex(kerr, "Code Segment", frame->CS, boxWidth);
        PrintBoxedHex(kerr, "Flags", frame->Flags, boxWidth);
        PrintBoxedHex(kerr, "Stack Pointer", frame->SP, boxWidth);
        PrintBoxedHex(kerr, "Stack Segment", frame->SS, boxWidth);
    }
#endif

    PrintBoxedLine(kerr, "", boxWidth);

    // Footer
    kerr << BOXUI_BL;
    for (int i = 0; i < boxWidth - 2; ++i) kerr << BOXUI_H;
    kerr << BOXUI_BR << "\n";
    kerr << BOXUI_ANSI_RESET;

    while (true) {
#if defined (__x86_64__)
        asm ("cli");
        asm ("hlt");
#elif defined (__aarch64__) || defined (__riscv)
        asm ("wfi");
#elif defined (__loongarch64)
        asm ("idle 0");
#endif
    }
}