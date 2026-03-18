/*
 * collision.h
 * MontaukOS 2D Game Engine - Collision Detection
 * AABB overlap testing and tile-based collision response
 * Copyright (c) 2026 Daniel Hammer
 */

#pragma once
#include <cstdint>
#include "engine/tilemap.h"

namespace engine {

struct AABB {
    float x, y, w, h;

    bool overlaps(const AABB& other) const {
        return x < other.x + other.w && x + w > other.x &&
               y < other.y + other.h && y + h > other.y;
    }

    bool contains_point(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

// Try to move an entity by (dx, dy) on the tilemap.
// Uses separate X/Y axis resolution so the entity slides along walls.
// box: the entity's collision box in world pixel coordinates.
// Returns adjusted dx, dy after collision.
inline void resolve_tilemap_collision(const Tilemap& map,
                                      float bx, float by, float bw, float bh,
                                      float& dx, float& dy) {
    // Try X movement
    float new_x = bx + dx;
    if (map.collides((int)new_x, (int)by, (int)bw, (int)bh)) {
        dx = 0.0f;
        new_x = bx;
    }

    // Try Y movement
    float new_y = by + dy;
    if (map.collides((int)new_x, (int)new_y, (int)bw, (int)bh)) {
        dy = 0.0f;
    }
}

} // namespace engine
