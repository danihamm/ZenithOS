/*
    * app_wiki.cpp
    * ZenithOS Desktop - Wikipedia launcher
    * Spawns wikipedia.elf as a standalone Window Server process
    * Copyright (c) 2026 Daniel Hammer
*/

#include "apps_common.hpp"

void open_wiki(DesktopState* ds) {
    (void)ds;
    zenith::spawn("0:/os/wikipedia.elf");
}
