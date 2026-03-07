/*
    * app_devexplorer.cpp
    * MontaukOS Desktop - Device Explorer launcher (spawns standalone devexplorer.elf)
    * Copyright (c) 2026 Daniel Hammer
*/

#include "apps_common.hpp"

void open_devexplorer(DesktopState* ds) {
    (void)ds;
    montauk::spawn("0:/os/devexplorer.elf");
}
