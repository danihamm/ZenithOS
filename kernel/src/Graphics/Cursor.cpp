/*
    * Cursor.cpp
    * Framebuffer information storage
    * Copyright (c) 2025 Daniel Hammer
*/

#include "Cursor.hpp"
#include <Terminal/Terminal.hpp>
#include <Memory/HHDM.hpp>

namespace Graphics::Cursor {

    // Framebuffer state
    static uint32_t* g_FbBase   = nullptr;
    static uint64_t  g_FbWidth  = 0;
    static uint64_t  g_FbHeight = 0;
    static uint64_t  g_FbPitch  = 0; // in bytes

    void Initialize(limine_framebuffer* framebuffer) {
        g_FbBase   = reinterpret_cast<uint32_t*>(framebuffer->address);
        g_FbWidth  = framebuffer->width;
        g_FbHeight = framebuffer->height;
        g_FbPitch  = framebuffer->pitch;

        Kt::KernelLogStream(Kt::OK, "Graphics") << "Framebuffer initialized ("
            << (uint64_t)g_FbWidth << "x" << (uint64_t)g_FbHeight << ")";
    }

    uint32_t* GetFramebufferBase()   { return g_FbBase; }
    uint64_t  GetFramebufferWidth()  { return g_FbWidth; }
    uint64_t  GetFramebufferHeight() { return g_FbHeight; }
    uint64_t  GetFramebufferPitch()  { return g_FbPitch; }

    void SetFramebuffer(uint32_t* base, uint64_t width, uint64_t height, uint64_t pitch) {
        g_FbBase = base;
        g_FbWidth = width;
        g_FbHeight = height;
        g_FbPitch = pitch;
        Kt::KernelLogStream(Kt::OK, "Graphics") << "Framebuffer switched ("
            << (uint64_t)g_FbWidth << "x" << (uint64_t)g_FbHeight << ")";
    }

    uint64_t GetFramebufferPhysBase() {
        return Memory::SubHHDM((uint64_t)g_FbBase);
    }

};
