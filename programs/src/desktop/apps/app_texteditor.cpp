/*
    * app_texteditor.cpp
    * MontaukOS Desktop - Text Editor launcher
    * Spawns standalone texteditor.elf Window Server app
    * Copyright (c) 2026 Daniel Hammer
*/

#include "apps_common.hpp"

// ============================================================================
// Text Editor launchers (spawn standalone Window Server app)
// ============================================================================

void open_texteditor(DesktopState* ds) {
    (void)ds;
    montauk::spawn("0:/apps/texteditor/texteditor.elf");
}

void open_texteditor_with_file(DesktopState* ds, const char* path) {
    (void)ds;
    montauk::spawn("0:/apps/texteditor/texteditor.elf", path);
}
