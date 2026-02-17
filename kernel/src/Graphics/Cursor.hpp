/*
    * Cursor.hpp
    * Simple framebuffer mouse cursor
    * Copyright (c) 2025 Daniel Hammer
*/

#pragma once
#include <cstdint>
#include <limine.h>

namespace Graphics::Cursor {

    void Initialize(limine_framebuffer* framebuffer);
    void Update();

};
