/*
 * stb_image_write_impl.c
 * Single compilation unit for stb_image_write JPEG encoder on MontaukOS
 * Copyright (c) 2026 Daniel Hammer
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Only enable JPEG writing. */
#define STBI_WRITE_NO_STDIO

/* Route allocations through libc (which uses MontaukOS heap). */
#define STBIW_MALLOC(sz)         malloc(sz)
#define STBIW_FREE(p)            free(p)
#define STBIW_REALLOC(p, newsz)  realloc(p, newsz)
#define STBIW_MEMMOVE(d, s, n)   memmove(d, s, n)

#define STBIW_ASSERT(x)  ((void)(x))

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <gui/stb_image_write.h>
