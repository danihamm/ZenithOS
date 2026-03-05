/*
    * app_spreadsheet.cpp
    * MontaukOS Desktop - Spreadsheet launcher (spawns standalone spreadsheet.elf)
    * Copyright (c) 2026 Daniel Hammer
*/

#include "apps_common.hpp"

void open_spreadsheet(DesktopState* ds) {
    (void)ds;
    montauk::spawn("0:/os/spreadsheet.elf");
}
