/*
    * app_terminal.cpp
    * MontaukOS Desktop - Terminal launcher
    * Spawns standalone terminal.elf Window Server app
    * Copyright (c) 2026 Daniel Hammer
*/

#include "apps_common.hpp"

void open_terminal(DesktopState* ds) {
    if (ds && ds->home_dir[0] != '\0') {
        montauk::spawn("0:/apps/terminal/terminal.elf", ds->home_dir);
    } else {
        montauk::spawn("0:/apps/terminal/terminal.elf");
    }
}
