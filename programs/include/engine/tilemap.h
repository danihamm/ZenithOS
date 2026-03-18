/*
 * tilemap.h
 * MontaukOS 2D Game Engine - Tile Map
 * Grid-based terrain with per-tile rendering and collision
 * Copyright (c) 2026 Daniel Hammer
 */

#pragma once
#include <cstdint>
#include <montauk/heap.h>
#include <montauk/string.h>
#include "engine/sprite.h"

namespace engine {

static constexpr int MAX_TILE_TYPES = 16;

struct TileType {
    Spritesheet* sheet;   // tile image (may be a single-tile spritesheet)
    int src_x, src_y;     // source position within the sheet
    int src_w, src_h;     // source size (typically tile_size x tile_size)
    bool solid;           // blocks movement
};

struct Tilemap {
    int* data = nullptr;
    int map_w = 0;
    int map_h = 0;
    int tile_size = 16;   // native tile size in pixels

    TileType types[MAX_TILE_TYPES];
    int type_count = 0;

    bool alloc(int w, int h, int ts = 16) {
        map_w = w;
        map_h = h;
        tile_size = ts;
        data = (int*)montauk::malloc(w * h * sizeof(int));
        if (!data) return false;
        montauk::memset(data, 0, w * h * sizeof(int));
        return true;
    }

    void free_map() {
        if (data) { montauk::mfree(data); data = nullptr; }
    }

    // Register a tile type. Returns the tile ID.
    int add_type(Spritesheet* sheet, int sx, int sy, int sw, int sh, bool solid) {
        if (type_count >= MAX_TILE_TYPES) return -1;
        int id = type_count++;
        types[id].sheet = sheet;
        types[id].src_x = sx;
        types[id].src_y = sy;
        types[id].src_w = sw;
        types[id].src_h = sh;
        types[id].solid = solid;
        return id;
    }

    void set(int x, int y, int tile_id) {
        if (x >= 0 && x < map_w && y >= 0 && y < map_h)
            data[y * map_w + x] = tile_id;
    }

    int get(int x, int y) const {
        if (x < 0 || x >= map_w || y < 0 || y >= map_h) return -1;
        return data[y * map_w + x];
    }

    bool is_solid(int x, int y) const {
        int id = get(x, y);
        if (id < 0 || id >= type_count) return true; // out of bounds = solid
        return types[id].solid;
    }

    // Check if a world-pixel rectangle collides with any solid tile.
    // World coordinates are in native (unscaled) pixels.
    bool collides(int wx, int wy, int ww, int wh) const {
        // Negative coordinates are always solid (out of bounds)
        if (wx < 0 || wy < 0) return true;
        int tx0 = wx / tile_size;
        int ty0 = wy / tile_size;
        int tx1 = (wx + ww - 1) / tile_size;
        int ty1 = (wy + wh - 1) / tile_size;

        for (int ty = ty0; ty <= ty1; ty++)
            for (int tx = tx0; tx <= tx1; tx++)
                if (is_solid(tx, ty))
                    return true;
        return false;
    }

    // Draw visible tiles to the pixel buffer.
    // cam_x/cam_y: camera position in world pixels (native scale).
    // scale: rendering scale factor.
    void draw(uint32_t* dst, int dst_w, int dst_h,
              int cam_x, int cam_y, int scale) const {
        if (!data) return;

        int ts = tile_size * scale;

        // Determine visible tile range
        int tx0 = cam_x / tile_size;
        int ty0 = cam_y / tile_size;
        int tx1 = tx0 + dst_w / ts + 2;
        int ty1 = ty0 + dst_h / ts + 2;

        if (tx0 < 0) tx0 = 0;
        if (ty0 < 0) ty0 = 0;
        if (tx1 > map_w) tx1 = map_w;
        if (ty1 > map_h) ty1 = map_h;

        for (int ty = ty0; ty < ty1; ty++) {
            for (int tx = tx0; tx < tx1; tx++) {
                int id = data[ty * map_w + tx];
                if (id < 0 || id >= type_count) continue;

                const TileType& tt = types[id];
                if (!tt.sheet) continue;

                int sx = tx * ts - cam_x * scale;
                int sy = ty * ts - cam_y * scale;

                tt.sheet->draw_region(dst, dst_w, dst_h,
                                      tt.src_x, tt.src_y,
                                      tt.src_w, tt.src_h,
                                      sx, sy, scale);
            }
        }
    }
};

} // namespace engine
