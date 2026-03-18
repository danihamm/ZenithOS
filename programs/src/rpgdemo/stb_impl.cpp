/*
 * stb_impl.cpp
 * Single compilation unit for stb_image (PNG) and stb_truetype
 * for MontaukOS freestanding environment
 * Copyright (c) 2026 Daniel Hammer
 */

#include <cstdint>
#include <cstddef>
#include <montauk/heap.h>
#include <montauk/string.h>
#include <gui/stb_math.h>

extern "C" {
#include <stdlib.h>
#include <string.h>
}

// ============================================================================
// stb_image - PNG decoder
// ============================================================================

#define STBI_ONLY_PNG
#define STBI_NO_JPEG
#define STBI_NO_BMP
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_LINEAR
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS

#define STBI_MALLOC(sz)         malloc(sz)
#define STBI_FREE(p)            free(p)
#define STBI_REALLOC(p, newsz)  realloc(p, newsz)

#define STBI_ASSERT(x)          ((void)(x))

#define STB_IMAGE_IMPLEMENTATION
#include <gui/stb_image.h>

// ============================================================================
// stb_truetype - TrueType font renderer
// ============================================================================

#define STBTT_ifloor(x)   ((int) stb_floor(x))
#define STBTT_iceil(x)    ((int) stb_ceil(x))
#define STBTT_sqrt(x)     stb_sqrt(x)
#define STBTT_pow(x,y)    stb_pow(x,y)
#define STBTT_fmod(x,y)   stb_fmod(x,y)
#define STBTT_cos(x)      stb_cos(x)
#define STBTT_acos(x)     stb_acos(x)
#define STBTT_fabs(x)     stb_fabs(x)

#define STBTT_malloc(x,u)  ((void)(u), montauk::malloc(x))
#define STBTT_free(x,u)    ((void)(u), montauk::mfree(x))

#define STBTT_memcpy(d,s,n) montauk::memcpy(d,s,n)
#define STBTT_memset(d,v,n) montauk::memset(d,v,n)

#define STBTT_strlen(x)    montauk::slen(x)

#define STBTT_assert(x)    ((void)(x))

#define STB_TRUETYPE_IMPLEMENTATION
#include <gui/stb_truetype.h>
