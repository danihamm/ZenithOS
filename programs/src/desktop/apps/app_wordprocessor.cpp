/*
    * app_wordprocessor.cpp
    * MontaukOS Desktop - Word Processor launcher
    * Spawns standalone wordprocessor.elf Window Server app
    * Copyright (c) 2026 Daniel Hammer
*/

#include "apps_common.hpp"

void open_wordprocessor(DesktopState* ds) {
    (void)ds;
    montauk::spawn("0:/apps/wordprocessor/wordprocessor.elf");
}
