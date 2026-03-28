/*
    * app_klog.cpp
    * MontaukOS Desktop - Kernel Log launcher
    * Spawns standalone klog.elf Window Server app
    * Copyright (c) 2026 Daniel Hammer
*/

#include "apps_common.hpp"

void open_klog(DesktopState* ds) {
    (void)ds;
    montauk::spawn("0:/apps/klog/klog.elf");
}
