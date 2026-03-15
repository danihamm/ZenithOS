/*
    * Panic.cpp
    * Copyright (c) 2025 Daniel Hammer
*/

#include "Panic.hpp"
#include "../CppLib/BoxUI.hpp"

static constexpr int BoxWidth = 72;

static void PrintHorizontalEdge(const char* left, const char* right) {
    kerr << left;
    for (int i = 0; i < BoxWidth - 2; ++i) kerr << BOXUI_H;
    kerr << right << "\n";
}

static void PrintPageFaultInfo(System::PanicFrame*& frame) {
    auto* pf = (System::PageFaultPanicFrame*)frame;
    frame = (System::PanicFrame*)&pf->IP;

    uint64_t cr2;
    asm volatile("mov %%cr2, %0" : "=r"(cr2));
    PrintBoxedHex(kerr, "Faulting Address (CR2)", cr2, BoxWidth);

    PrintBoxedLine(kerr, "Page Fault Error:", BoxWidth, true);
    PrintBoxedDec(kerr, "Present",          pf->PageFaultError.Present,          BoxWidth);
    PrintBoxedDec(kerr, "Write",            pf->PageFaultError.Write,            BoxWidth);
    PrintBoxedDec(kerr, "User",             pf->PageFaultError.User,             BoxWidth);
    PrintBoxedDec(kerr, "Reserved Write",   pf->PageFaultError.ReservedWrite,    BoxWidth);
    PrintBoxedDec(kerr, "Instruction Fetch",pf->PageFaultError.InstructionFetch, BoxWidth);
    PrintBoxedDec(kerr, "Protection Key",   pf->PageFaultError.ProtectionKey,    BoxWidth);
    PrintBoxedDec(kerr, "Shadow Stack",     pf->PageFaultError.ShadowStack,      BoxWidth);
    PrintBoxedDec(kerr, "SGX",              pf->PageFaultError.SGX,              BoxWidth);
}

static void PrintGPFInfo(System::PanicFrame*& frame) {
    auto* gpf = (System::GPFPanicFrame*)frame;
    frame = (System::PanicFrame*)&gpf->IP;

    PrintBoxedLine(kerr, "General Protection Fault:", BoxWidth, true);
    PrintBoxedDec(kerr, "Error Code", gpf->GeneralProtectionFaultError, BoxWidth);
}

static void PrintRegisters(System::PanicFrame* frame) {
    PrintBoxedSeparator(kerr, BoxWidth);
    PrintBoxedLine(kerr, "Registers:", BoxWidth, true);
    PrintBoxedHex(kerr, "Instruction Pointer", frame->IP,    BoxWidth);
    PrintBoxedHex(kerr, "Code Segment",        frame->CS,    BoxWidth);
    PrintBoxedHex(kerr, "Flags",               frame->Flags, BoxWidth);
    PrintBoxedHex(kerr, "Stack Pointer",       frame->SP,    BoxWidth);
    PrintBoxedHex(kerr, "Stack Segment",       frame->SS,    BoxWidth);
}

[[noreturn]] static void Halt() {
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

void Panic(const char *meditationString, System::PanicFrame* frame) {
    // Header
    kerr << BOXUI_ANSI_RED_BG << BOXUI_ANSI_WHITE_FG << BOXUI_ANSI_BOLD << "\n";
    PrintHorizontalEdge(BOXUI_TL, BOXUI_TR);
    PrintBoxedLine(kerr, "!!! KERNEL PANIC !!!", BoxWidth, true);
    PrintBoxedLine(kerr, "", BoxWidth);
    PrintBoxedLine(kerr, "System halted. Please reboot.", BoxWidth, true);
    PrintBoxedLine(kerr, "", BoxWidth);

    // Meditation string
    PrintBoxedSeparator(kerr, BoxWidth);
    PrintBoxedLine(kerr, meditationString, BoxWidth);
    PrintBoxedLine(kerr, "", BoxWidth);

    // CPU state (x86_64 only)
#if defined (__x86_64__)
    if (frame != nullptr) {
        PrintBoxedSeparator(kerr, BoxWidth);
        PrintBoxedLine(kerr, "CPU State:", BoxWidth, true);
        PrintBoxedHex(kerr, "Interrupt Vector", frame->InterruptVector, BoxWidth);

        if (frame->InterruptVector == 0xE)
            PrintPageFaultInfo(frame);
        else if (frame->InterruptVector == 0xD)
            PrintGPFInfo(frame);

        PrintRegisters(frame);
    }
#endif

    // Footer
    PrintBoxedLine(kerr, "", BoxWidth);
    PrintHorizontalEdge(BOXUI_BL, BOXUI_BR);
    kerr << BOXUI_ANSI_RESET;

    Halt();
}
