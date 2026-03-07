/*
    * app_disks.cpp
    * Copyright (c) 2026 Daniel Hammer
*/

#include "apps_common.hpp"

void open_disks(DesktopState* ds) {
    (void)ds;
    montauk::spawn("0:/os/disks.elf");
}
