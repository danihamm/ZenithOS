/*
 * sprite.h
 * MontaukOS 2D Game Engine - Sprite and Animation System
 * PNG spritesheet loading, frame extraction, alpha-blended rendering
 * Copyright (c) 2026 Daniel Hammer
 */

#pragma once
#include <cstdint>
#include <montauk/heap.h>
#include <montauk/string.h>
#include "engine/engine.h"

extern "C" {
#include <gui/stb_image.h>
}

namespace engine {

// ============================================================================
// Convert stb_image RGBA output to MontaukOS ARGB pixel format
// stb on little-endian x86: pixel bytes in memory are R,G,B,A
// When read as uint32_t: 0xAABBGGRR
// MontaukOS format: 0xAARRGGBB
// ============================================================================

inline void rgba_to_argb(uint32_t* data, int count) {
    for (int i = 0; i < count; i++) {
        uint32_t px = data[i];
        uint8_t r = px & 0xFF;
        uint8_t g = (px >> 8) & 0xFF;
        uint8_t b = (px >> 16) & 0xFF;
        uint8_t a = (px >> 24) & 0xFF;
        data[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                  ((uint32_t)g << 8) | b;
    }
}

// ============================================================================
// Spritesheet
// ============================================================================

struct Spritesheet {
    uint32_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    int frame_w = 0;
    int frame_h = 0;
    int cols = 0;
    int rows = 0;

    // Load a PNG spritesheet from VFS and split into frames of given size.
    // If frame_w/frame_h are 0, treat the entire image as a single frame.
    bool load(const char* vfs_path, int fw = 0, int fh = 0) {
        FileData file;
        if (!file.load(vfs_path)) return false;

        int w, h, channels;
        uint32_t* img = (uint32_t*)stbi_load_from_memory(
            file.data, (int)file.size, &w, &h, &channels, 4);
        file.free();

        if (!img) return false;

        // Convert RGBA to ARGB
        rgba_to_argb(img, w * h);

        pixels = img;
        width = w;
        height = h;
        frame_w = fw > 0 ? fw : w;
        frame_h = fh > 0 ? fh : h;
        cols = w / frame_w;
        rows = h / frame_h;
        return true;
    }

    void unload() {
        if (pixels) {
            stbi_image_free(pixels);
            pixels = nullptr;
        }
    }

    // Blit a single frame to the destination buffer with scaling and alpha.
    // frame_col/frame_row select which frame from the spritesheet.
    // dst_x/dst_y is the screen position. scale is the integer scale factor.
    // flip_h mirrors the sprite horizontally.
    void draw_frame(uint32_t* dst, int dst_w, int dst_h,
                    int frame_col, int frame_row,
                    int dst_x, int dst_y, int scale = 1,
                    bool flip_h = false) const {
        if (!pixels) return;
        if (frame_col < 0 || frame_col >= cols) return;
        if (frame_row < 0 || frame_row >= rows) return;

        int src_ox = frame_col * frame_w;
        int src_oy = frame_row * frame_h;
        int out_w = frame_w * scale;
        int out_h = frame_h * scale;

        for (int py = 0; py < out_h; py++) {
            int dy = dst_y + py;
            if (dy < 0 || dy >= dst_h) continue;
            int sy = src_oy + py / scale;

            for (int px = 0; px < out_w; px++) {
                int dx = dst_x + px;
                if (dx < 0 || dx >= dst_w) continue;

                int sx_local = px / scale;
                if (flip_h) sx_local = frame_w - 1 - sx_local;
                int sx = src_ox + sx_local;

                uint32_t src_px = pixels[sy * width + sx];
                uint8_t sa = (src_px >> 24) & 0xFF;
                if (sa == 0) continue;

                if (sa == 255) {
                    dst[dy * dst_w + dx] = src_px;
                } else {
                    uint32_t d = dst[dy * dst_w + dx];
                    uint8_t sr = (src_px >> 16) & 0xFF;
                    uint8_t sg = (src_px >> 8) & 0xFF;
                    uint8_t sb = src_px & 0xFF;
                    uint8_t dr = (d >> 16) & 0xFF;
                    uint8_t dg = (d >> 8) & 0xFF;
                    uint8_t db = d & 0xFF;
                    uint32_t inv = 255 - sa;
                    uint32_t rr = (sa * sr + inv * dr + 128) / 255;
                    uint32_t gg = (sa * sg + inv * dg + 128) / 255;
                    uint32_t bb = (sa * sb + inv * db + 128) / 255;
                    dst[dy * dst_w + dx] =
                        0xFF000000 | (rr << 16) | (gg << 8) | bb;
                }
            }
        }
    }

    // Draw an arbitrary sub-rectangle of the spritesheet (not frame-aligned)
    void draw_region(uint32_t* dst, int dst_w, int dst_h,
                     int src_x, int src_y, int src_w, int src_h,
                     int dst_x, int dst_y, int scale = 1) const {
        if (!pixels) return;

        int out_w = src_w * scale;
        int out_h = src_h * scale;

        for (int py = 0; py < out_h; py++) {
            int dy = dst_y + py;
            if (dy < 0 || dy >= dst_h) continue;
            int sy = src_y + py / scale;
            if (sy < 0 || sy >= height) continue;

            for (int px = 0; px < out_w; px++) {
                int dx = dst_x + px;
                if (dx < 0 || dx >= dst_w) continue;
                int sx = src_x + px / scale;
                if (sx < 0 || sx >= width) continue;

                uint32_t src_px = pixels[sy * width + sx];
                uint8_t sa = (src_px >> 24) & 0xFF;
                if (sa == 0) continue;

                if (sa == 255) {
                    dst[dy * dst_w + dx] = src_px;
                } else {
                    uint32_t d = dst[dy * dst_w + dx];
                    uint8_t sr = (src_px >> 16) & 0xFF;
                    uint8_t sg = (src_px >> 8) & 0xFF;
                    uint8_t sb = src_px & 0xFF;
                    uint8_t dr = (d >> 16) & 0xFF;
                    uint8_t dg = (d >> 8) & 0xFF;
                    uint8_t db = d & 0xFF;
                    uint32_t inv = 255 - sa;
                    uint32_t rr = (sa * sr + inv * dr + 128) / 255;
                    uint32_t gg = (sa * sg + inv * dg + 128) / 255;
                    uint32_t bb = (sa * sb + inv * db + 128) / 255;
                    dst[dy * dst_w + dx] =
                        0xFF000000 | (rr << 16) | (gg << 8) | bb;
                }
            }
        }
    }
};

// ============================================================================
// Animation
// ============================================================================

struct Animation {
    int row = 0;         // spritesheet row for this animation
    int start_col = 0;   // first frame column
    int num_frames = 1;  // number of frames
    float speed = 8.0f;  // frames per second

    float timer = 0.0f;
    int current = 0;

    void update(float dt) {
        timer += dt * speed;
        while (timer >= 1.0f) {
            timer -= 1.0f;
            current++;
            if (current >= num_frames) current = 0;
        }
    }

    void reset() {
        timer = 0.0f;
        current = 0;
    }

    int frame_col() const { return start_col + current; }
    int frame_row() const { return row; }
};

// ============================================================================
// Animated Sprite - combines a spritesheet with named animations
// ============================================================================

static constexpr int MAX_ANIMS = 16;

struct AnimatedSprite {
    Spritesheet* sheet = nullptr;
    Animation anims[MAX_ANIMS];
    int anim_count = 0;
    int current_anim = 0;
    bool flip_h = false;

    int add_anim(int row, int start_col, int num_frames, float speed = 8.0f) {
        if (anim_count >= MAX_ANIMS) return -1;
        int idx = anim_count++;
        anims[idx].row = row;
        anims[idx].start_col = start_col;
        anims[idx].num_frames = num_frames;
        anims[idx].speed = speed;
        return idx;
    }

    void play(int anim_idx) {
        if (anim_idx < 0 || anim_idx >= anim_count) return;
        if (current_anim != anim_idx) {
            current_anim = anim_idx;
            anims[current_anim].reset();
        }
    }

    void update(float dt) {
        if (current_anim >= 0 && current_anim < anim_count)
            anims[current_anim].update(dt);
    }

    void draw(uint32_t* dst, int dst_w, int dst_h,
              int x, int y, int scale = 1) const {
        if (!sheet || current_anim < 0 || current_anim >= anim_count) return;
        const Animation& a = anims[current_anim];
        sheet->draw_frame(dst, dst_w, dst_h,
                          a.frame_col(), a.frame_row(),
                          x, y, scale, flip_h);
    }
};

} // namespace engine
