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

    uint32_t* GetFramebufferBase();
    uint64_t  GetFramebufferWidth();
    uint64_t  GetFramebufferHeight();
    uint64_t  GetFramebufferPitch();

};
