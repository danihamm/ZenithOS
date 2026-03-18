/*
 * main.cpp
 * MontaukOS RPG Demo - "Montauk Quest"
 * A 2D fantasy RPG demonstrating the MontaukOS game engine
 * Copyright (c) 2026 Daniel Hammer
 */

#include <montauk/syscall.h>
#include <montauk/string.h>
#include <montauk/heap.h>
#include <gui/gui.hpp>
#include <gui/truetype.hpp>

#include <engine/engine.h>
#include <engine/sprite.h>
#include <engine/tilemap.h>
#include <engine/input.h>
#include <engine/audio.h>
#include <engine/ui.h>
#include <engine/collision.h>

extern "C" {
#include <stdio.h>
#include <string.h>
}

using namespace engine;
using namespace gui;

// ============================================================================
// Constants
// ============================================================================

static constexpr int WIN_W      = 800;
static constexpr int WIN_H      = 600;
static constexpr int SCALE      = 2;     // pixel art scale
static constexpr int TILE_SIZE  = 16;    // native tile size
static constexpr int MAP_W      = 50;    // map width in tiles
static constexpr int MAP_H      = 40;    // map height in tiles
static constexpr float PLAYER_SPEED = 80.0f; // pixels/sec (native scale)
static constexpr float ENEMY_SPEED  = 30.0f;
static constexpr int PLAYER_HP  = 100;
static constexpr int MAX_ENEMIES     = 8;
static constexpr int MAX_DECORATIONS = 96;
static constexpr int MAX_CHESTS      = 6;

// Player sprite layout (32x32 frames, 6 cols x 10 rows)
static constexpr int SPR_W = 32;
static constexpr int SPR_H = 32;

// Directions
enum Dir { DIR_DOWN = 0, DIR_RIGHT = 1, DIR_UP = 2, DIR_LEFT = 3 };

// ============================================================================
// Entity types
// ============================================================================

struct Player {
    float x, y;          // world position (native pixels)
    int health;
    int max_health;
    int direction;
    bool moving;
    int score;
    float hit_timer;     // invincibility after taking damage
    AnimatedSprite sprite;
    int anim_walk[4];    // walk anim indices per direction
    int anim_idle[4];    // idle anim indices per direction
};

struct Enemy {
    float x, y;
    int health;
    int max_health;
    int direction;
    bool active;
    float patrol_timer;
    float patrol_duration;
    AnimatedSprite sprite;
    int anim_walk[4];
    int type;            // 0 = skeleton, 1 = slime
};

struct Decoration {
    float x, y;
    Spritesheet* sheet;
    bool solid;
    float col_x, col_y, col_w, col_h; // collision box offset
    int frame_col, frame_row;          // -1 = draw full sheet
};

struct Chest {
    float x, y;
    bool opened;
    bool collected;
};

// ============================================================================
// Game state
// ============================================================================

static Engine g_engine;
static InputState g_input;
static AudioEngine g_audio;
static Tilemap g_map;

// Spritesheets
static Spritesheet g_spr_player;
static Spritesheet g_spr_skeleton;
static Spritesheet g_spr_slime;
static Spritesheet g_spr_grass;
static Spritesheet g_spr_path;
static Spritesheet g_spr_water;
static Spritesheet g_spr_tree;
static Spritesheet g_spr_tree_small;
static Spritesheet g_spr_house;
static Spritesheet g_spr_chest;
static Spritesheet g_spr_fences;
static Spritesheet g_spr_gravestone;

// Entities
static Player g_player;
static Enemy g_enemies[MAX_ENEMIES];
static int g_enemy_count = 0;
static Decoration g_decor[MAX_DECORATIONS];
static int g_decor_count = 0;
static Chest g_chests[MAX_CHESTS];
static int g_chest_count = 0;

// Camera
static float g_cam_x = 0;
static float g_cam_y = 0;

// UI state
static char g_prompt[128] = {};
static char g_dialog_text[256] = {};
static float g_dialog_timer = 0;

// ============================================================================
// Asset loading
// ============================================================================

static bool load_assets() {
    const char* base = "0:/apps/rpgdemo/";
    char path[128];

    auto make_path = [&](const char* file) {
        snprintf(path, sizeof(path), "%s%s", base, file);
    };

    // Player (32x32 frames, 6 columns x 10 rows)
    make_path("Player.png");
    if (!g_spr_player.load(path, SPR_W, SPR_H)) return false;

    // Skeleton (same layout as player)
    make_path("Skeleton.png");
    if (!g_spr_skeleton.load(path, SPR_W, SPR_H)) return false;

    // Slime (64x64 frames)
    make_path("Slime_Green.png");
    g_spr_slime.load(path, 64, 64); // not fatal if missing

    // Tiles (single 16x16 images)
    make_path("Grass_Middle.png");
    if (!g_spr_grass.load(path, TILE_SIZE, TILE_SIZE)) return false;

    make_path("Path_Middle.png");
    if (!g_spr_path.load(path, TILE_SIZE, TILE_SIZE)) return false;

    make_path("Water_Middle.png");
    if (!g_spr_water.load(path, TILE_SIZE, TILE_SIZE)) return false;

    // Decorations
    make_path("Oak_Tree.png");
    g_spr_tree.load(path);

    make_path("Oak_Tree_Small.png");
    g_spr_tree_small.load(path, 32, 48);

    make_path("House.png");
    g_spr_house.load(path);

    make_path("Chest.png");
    g_spr_chest.load(path, TILE_SIZE, TILE_SIZE);

    make_path("Fences.png");
    g_spr_fences.load(path, TILE_SIZE, TILE_SIZE);

    // Procedural gravestone sprite (16x16)
    {
        static const uint8_t bmp[16 * 16] = {
            0,0,0,0,0,0,2,2,2,2,0,0,0,0,0,0,
            0,0,0,0,0,2,1,1,1,1,2,0,0,0,0,0,
            0,0,0,0,2,1,1,1,1,1,1,2,0,0,0,0,
            0,0,0,0,2,1,1,1,1,1,1,2,0,0,0,0,
            0,0,0,0,2,1,1,2,1,1,1,2,0,0,0,0,
            0,0,0,0,2,1,2,2,2,1,1,2,0,0,0,0,
            0,0,0,0,2,1,1,2,1,1,1,2,0,0,0,0,
            0,0,0,0,2,1,1,2,1,1,1,2,0,0,0,0,
            0,0,0,0,2,1,1,1,1,1,1,2,0,0,0,0,
            0,0,0,0,2,1,1,1,1,1,1,2,0,0,0,0,
            0,0,0,0,2,1,1,1,1,1,1,2,0,0,0,0,
            0,0,0,0,2,1,1,1,1,1,1,2,0,0,0,0,
            0,0,0,0,2,2,2,2,2,2,2,2,0,0,0,0,
            0,0,0,3,3,3,3,3,3,3,3,3,3,0,0,0,
            0,0,0,0,0,3,3,3,3,3,3,0,0,0,0,0,
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        };
        static const uint32_t pal[4] = {
            0x00000000, 0xFF999999, 0xFF555555, 0xFF665544,
        };
        g_spr_gravestone.width = 16;
        g_spr_gravestone.height = 16;
        g_spr_gravestone.frame_w = 16;
        g_spr_gravestone.frame_h = 16;
        g_spr_gravestone.cols = 1;
        g_spr_gravestone.rows = 1;
        g_spr_gravestone.pixels = (uint32_t*)montauk::malloc(16 * 16 * 4);
        if (g_spr_gravestone.pixels) {
            for (int i = 0; i < 16 * 16; i++)
                g_spr_gravestone.pixels[i] = pal[bmp[i]];
        }
    }

    return true;
}

// ============================================================================
// Entity initialization
// ============================================================================

enum TileId { TILE_GRASS = 0, TILE_PATH = 1, TILE_WATER = 2 };

static void setup_player_anims(AnimatedSprite& spr, Spritesheet* sheet) {
    spr.sheet = sheet;
    for (int d = 0; d < 4; d++)
        spr.add_anim(d, 0, 6, 10.0f);
    for (int d = 0; d < 4; d++)
        spr.add_anim(d, 0, 1, 1.0f);
}

static void init_player() {
    g_player.x = 24 * TILE_SIZE;
    g_player.y = 22 * TILE_SIZE;
    g_player.health = PLAYER_HP;
    g_player.max_health = PLAYER_HP;
    g_player.direction = DIR_DOWN;
    g_player.moving = false;
    g_player.score = 0;
    g_player.hit_timer = 0;

    setup_player_anims(g_player.sprite, &g_spr_player);
    for (int d = 0; d < 4; d++) {
        g_player.anim_walk[d] = d;
        g_player.anim_idle[d] = 4 + d;
    }
    g_player.sprite.play(g_player.anim_idle[DIR_DOWN]);
}

static void add_enemy(float x, float y, int type) {
    if (g_enemy_count >= MAX_ENEMIES) return;
    Enemy& e = g_enemies[g_enemy_count++];
    e.x = x;
    e.y = y;
    e.health = 30;
    e.max_health = 30;
    e.direction = DIR_DOWN;
    e.active = true;
    e.patrol_timer = 0;
    e.patrol_duration = 2.0f;
    e.type = type;

    if (type == 0) {
        setup_player_anims(e.sprite, &g_spr_skeleton);
        for (int d = 0; d < 4; d++)
            e.anim_walk[d] = d;
    } else {
        e.sprite.sheet = &g_spr_slime;
        e.sprite.add_anim(0, 0, 4, 6.0f);
        for (int d = 0; d < 4; d++)
            e.anim_walk[d] = 0;
    }
    e.sprite.play(0);
}

static void add_decoration(float x, float y, Spritesheet* sheet,
                           bool solid, float cx, float cy, float cw, float ch,
                           int fcol = -1, int frow = -1) {
    if (g_decor_count >= MAX_DECORATIONS) return;
    Decoration& d = g_decor[g_decor_count++];
    d.x = x;
    d.y = y;
    d.sheet = sheet;
    d.solid = solid;
    d.col_x = cx;
    d.col_y = cy;
    d.col_w = cw;
    d.col_h = ch;
    d.frame_col = fcol;
    d.frame_row = frow;
}

static void add_chest(float x, float y) {
    if (g_chest_count >= MAX_CHESTS) return;
    Chest& c = g_chests[g_chest_count++];
    c.x = x;
    c.y = y;
    c.opened = false;
    c.collected = false;
}

// ============================================================================
// World Generation
// ============================================================================
//
// Uses an occupancy grid to prevent overlapping placements. Each tile cell
// stores bit flags indicating what occupies it. All placement functions
// validate against the grid before committing.
//
// Generation order (later phases respect earlier placements):
//   1. Water bodies  -> OCC_WATER
//   2. Path network  -> OCC_PATH
//   3. Houses        -> OCC_VISUAL + OCC_SOLID (base)
//   4. Fences        -> OCC_SOLID
//   5. Trees         -> OCC_VISUAL + OCC_SOLID (trunk)
//   6. Gravestones   -> OCC_VISUAL + OCC_SOLID
//   7. Chests        -> OCC_SOLID
//   8. Enemies       -> OCC_SPAWN
//
// ============================================================================

// ---- Occupancy grid ----

enum OccFlag : uint16_t {
    OCC_WATER  = 1 << 0,
    OCC_PATH   = 1 << 1,
    OCC_SOLID  = 1 << 2,
    OCC_VISUAL = 1 << 3,
    OCC_SPAWN  = 1 << 4,
};

static uint16_t g_occ[MAP_W * MAP_H];

static uint16_t occ_at(int tx, int ty) {
    if (tx < 0 || tx >= MAP_W || ty < 0 || ty >= MAP_H) return 0xFFFF;
    return g_occ[ty * MAP_W + tx];
}

static bool occ_free(int tx, int ty, int tw, int th, uint16_t mask) {
    for (int y = ty; y < ty + th; y++)
        for (int x = tx; x < tx + tw; x++)
            if (occ_at(x, y) & mask) return false;
    return true;
}

static void occ_mark(int tx, int ty, int tw, int th, uint16_t flags) {
    for (int y = ty; y < ty + th; y++)
        for (int x = tx; x < tx + tw; x++) {
            if (x >= 0 && x < MAP_W && y >= 0 && y < MAP_H)
                g_occ[y * MAP_W + x] |= flags;
        }
}

// ---- Terrain helpers ----

static void set_tiles(int x1, int y1, int x2, int y2, int tile, uint16_t occ) {
    for (int y = y1; y <= y2; y++)
        for (int x = x1; x <= x2; x++) {
            if (x >= 0 && x < MAP_W && y >= 0 && y < MAP_H) {
                g_map.set(x, y, tile);
                occ_mark(x, y, 1, 1, occ);
            }
        }
}

// ---- Placement functions ----

// Tree: 64x80px = 4w x 5h tiles
// Trunk collision at pixel (16,48) size (32,28) -> tile offset (1,3) size 2x2
static bool place_tree(int tx, int ty) {
    if (!occ_free(tx, ty, 4, 5, OCC_VISUAL | OCC_SOLID | OCC_WATER))
        return false;
    if (!occ_free(tx + 1, ty + 3, 2, 2, OCC_PATH))
        return false;
    add_decoration((float)tx * 16, (float)ty * 16,
                   &g_spr_tree, true, 16, 48, 32, 28);
    occ_mark(tx, ty, 4, 5, OCC_VISUAL);
    occ_mark(tx + 1, ty + 3, 2, 2, OCC_SOLID);
    return true;
}

// House: 96x128px = 6w x 8h tiles
// Base collision at pixel (8,80) size (80,44) -> tile offset (0,5) size ~6x3
// Visual may overlap paths (roof overhang) but base must not.
static bool place_house(int tx, int ty) {
    if (!occ_free(tx, ty, 6, 8, OCC_VISUAL | OCC_SOLID | OCC_WATER))
        return false;
    if (!occ_free(tx, ty + 5, 6, 3, OCC_PATH))
        return false;
    add_decoration((float)tx * 16, (float)ty * 16,
                   &g_spr_house, true, 8, 80, 80, 44);
    occ_mark(tx, ty, 6, 8, OCC_VISUAL);
    occ_mark(tx, ty + 5, 6, 3, OCC_SOLID);
    return true;
}

// Gravestone: 16x16px = 1x1 tile
static bool place_gravestone(int tx, int ty) {
    if (!occ_free(tx, ty, 1, 1, OCC_SOLID | OCC_VISUAL | OCC_WATER))
        return false;
    add_decoration((float)tx * 16, (float)ty * 16,
                   &g_spr_gravestone, true, 2, 2, 12, 14);
    occ_mark(tx, ty, 1, 1, OCC_SOLID | OCC_VISUAL);
    return true;
}

// Chest: 1x1 tile, must be on walkable ground
static bool place_chest_at(int tx, int ty) {
    if (!occ_free(tx, ty, 1, 1, OCC_SOLID | OCC_VISUAL | OCC_WATER))
        return false;
    add_chest((float)tx * 16, (float)ty * 16);
    occ_mark(tx, ty, 1, 1, OCC_SOLID);
    return true;
}

// Enemy: 2x2 tile footprint, needs walkable ground
static bool place_enemy_at(int tx, int ty, int type) {
    if (!occ_free(tx, ty, 2, 2, OCC_SOLID | OCC_WATER))
        return false;
    add_enemy((float)tx * 16, (float)ty * 16, type);
    occ_mark(tx, ty, 2, 2, OCC_SPAWN);
    return true;
}

// Fence runs - each tile validated, skipped if blocked
static void gen_fence_h(int tx, int ty, int length) {
    for (int i = 0; i < length; i++) {
        int x = tx + i;
        if (occ_at(x, ty) & (OCC_SOLID | OCC_WATER | OCC_PATH | OCC_VISUAL))
            continue;
        int col = (i == 0) ? 0 : (i == length - 1) ? 3 : ((i % 2 == 1) ? 1 : 2);
        add_decoration((float)x * 16, (float)ty * 16, &g_spr_fences,
                       true, 0, 4, 16, 12, col, 0);
        occ_mark(x, ty, 1, 1, OCC_SOLID);
    }
}

static void gen_fence_v(int tx, int ty, int length) {
    for (int i = 0; i < length; i++) {
        int y = ty + i;
        if (occ_at(tx, y) & (OCC_SOLID | OCC_WATER | OCC_PATH | OCC_VISUAL))
            continue;
        int row = (i == 0) ? 0 : (i == length - 1) ? 3 : ((i % 2 == 1) ? 1 : 2);
        add_decoration((float)tx * 16, (float)y * 16, &g_spr_fences,
                       true, 4, 0, 12, 16, 0, row);
        occ_mark(tx, y, 1, 1, OCC_SOLID);
    }
}

// ---- Main world generation ----

static void generate_world() {
    // Clear occupancy grid
    for (int i = 0; i < MAP_W * MAP_H; i++)
        g_occ[i] = 0;

    // Initialize tilemap
    g_map.alloc(MAP_W, MAP_H, TILE_SIZE);
    g_map.add_type(&g_spr_grass, 0, 0, TILE_SIZE, TILE_SIZE, false);
    g_map.add_type(&g_spr_path,  0, 0, TILE_SIZE, TILE_SIZE, false);
    g_map.add_type(&g_spr_water, 0, 0, TILE_SIZE, TILE_SIZE, true);
    for (int i = 0; i < MAP_W * MAP_H; i++)
        g_map.data[i] = TILE_GRASS;

    // ==== Phase 1: Water bodies ====

    // Lake (top-right, ellipse centered at tile 40,7)
    for (int y = 2; y < 12; y++) {
        for (int x = 34; x < 47; x++) {
            float dx = (float)(x - 40) / 6.0f;
            float dy = (float)(y - 7) / 4.5f;
            if (dx * dx + dy * dy < 1.0f) {
                g_map.set(x, y, TILE_WATER);
                occ_mark(x, y, 1, 1, OCC_WATER);
            }
        }
    }

    // Pond (west side)
    for (int y = 14; y < 18; y++) {
        for (int x = 8; x < 13; x++) {
            float dx = (float)(x - 10) / 2.5f;
            float dy = (float)(y - 16) / 1.8f;
            if (dx * dx + dy * dy < 1.0f) {
                g_map.set(x, y, TILE_WATER);
                occ_mark(x, y, 1, 1, OCC_WATER);
            }
        }
    }

    // ==== Phase 2: Path network ====

    set_tiles(0, 19, MAP_W - 1, 20, TILE_PATH, OCC_PATH);  // main E-W road
    set_tiles(24, 0, 25, MAP_H - 1, TILE_PATH, OCC_PATH);   // main N-S road
    set_tiles(25, 8, 42, 8, TILE_PATH, OCC_PATH);            // lake spur
    set_tiles(15, 19, 16, 30, TILE_PATH, OCC_PATH);          // south branch
    set_tiles(35, 20, 36, 26, TILE_PATH, OCC_PATH);          // graveyard access
    set_tiles(32, 26, 40, 33, TILE_PATH, OCC_PATH);          // graveyard floor

    // ==== Phase 3: Houses ====
    // Village north of crossroads. Houses are 6x8 tiles.
    // Positioned so bases (at ty+5..ty+7) don't overlap paths.

    place_house(18, 4);   // left house
    place_house(27, 4);   // right house (gap at N-S path cols 24-25)

    // ==== Phase 4: Village fences ====
    // South boundary with gate at N-S path (cols 24-25)

    gen_fence_h(18, 16, 6);   // left of gate:  tiles 18-23
    gen_fence_h(26, 16, 7);   // right of gate: tiles 26-32
    gen_fence_v(18, 12, 5);   // left side
    gen_fence_v(32, 12, 5);   // right side

    // ==== Phase 5: Trees ====

    // Northwest forest (4x5 tile spacing to prevent overlap)
    place_tree(0, 0);
    place_tree(5, 0);
    place_tree(10, 0);
    place_tree(2, 5);
    place_tree(7, 5);
    place_tree(12, 5);
    place_tree(0, 10);

    // South wilderness
    place_tree(2, 26);
    place_tree(8, 30);
    place_tree(28, 26);
    place_tree(43, 28);
    place_tree(44, 23);

    // East side
    place_tree(46, 13);
    place_tree(42, 21);

    // ==== Phase 6: Gravestones ====
    // Two rows of three within graveyard zone (32-40, 26-33)

    place_gravestone(34, 28);
    place_gravestone(36, 28);
    place_gravestone(38, 28);
    place_gravestone(34, 31);
    place_gravestone(36, 31);
    place_gravestone(38, 31);

    // ==== Phase 7: Chests ====

    place_chest_at(4, 3);     // forest clearing
    place_chest_at(43, 12);   // near lake
    place_chest_at(1, 32);    // southern wilderness
    place_chest_at(46, 35);   // far corner
    place_chest_at(26, 14);   // village yard

    // ==== Phase 8: Enemies ====

    // Skeletons patrol the graveyard
    place_enemy_at(34, 29, 0);
    place_enemy_at(37, 29, 0);
    place_enemy_at(35, 32, 0);

    // Slimes in the northwest forest
    place_enemy_at(4, 6, 1);
    place_enemy_at(10, 6, 1);

    // Skeleton guard near lake
    place_enemy_at(38, 14, 0);
}

// ============================================================================
// Update
// ============================================================================

static void update_camera() {
    // Center camera on player
    float target_x = g_player.x - (float)(WIN_W / SCALE) / 2.0f + SPR_W / 2.0f;
    float target_y = g_player.y - (float)(WIN_H / SCALE) / 2.0f + SPR_H / 2.0f;

    // Smooth follow
    g_cam_x += (target_x - g_cam_x) * 6.0f * g_engine.dt;
    g_cam_y += (target_y - g_cam_y) * 6.0f * g_engine.dt;

    // Clamp to map bounds
    float max_x = (float)(MAP_W * TILE_SIZE) - (float)(WIN_W / SCALE);
    float max_y = (float)(MAP_H * TILE_SIZE) - (float)(WIN_H / SCALE);
    if (g_cam_x < 0) g_cam_x = 0;
    if (g_cam_y < 0) g_cam_y = 0;
    if (g_cam_x > max_x) g_cam_x = max_x;
    if (g_cam_y > max_y) g_cam_y = max_y;
}

static bool check_decoration_collision(float x, float y, float w, float h) {
    for (int i = 0; i < g_decor_count; i++) {
        Decoration& d = g_decor[i];
        if (!d.solid) continue;
        AABB a = { x, y, w, h };
        AABB b = { d.x + d.col_x, d.y + d.col_y, d.col_w, d.col_h };
        if (a.overlaps(b)) return true;
    }
    return false;
}

static void update_player(float dt) {
    float dx = 0, dy = 0;
    bool moving = false;

    if (g_input.key_held(key::W) || g_input.key_held(key::UP))    { dy = -1; g_player.direction = DIR_UP; moving = true; }
    if (g_input.key_held(key::S) || g_input.key_held(key::DOWN))  { dy =  1; g_player.direction = DIR_DOWN; moving = true; }
    if (g_input.key_held(key::A) || g_input.key_held(key::LEFT))  { dx = -1; g_player.direction = DIR_LEFT; moving = true; }
    if (g_input.key_held(key::D) || g_input.key_held(key::RIGHT)) { dx =  1; g_player.direction = DIR_RIGHT; moving = true; }

    // Normalize diagonal movement
    if (dx != 0 && dy != 0) {
        dx *= 0.707f;
        dy *= 0.707f;
    }

    dx *= PLAYER_SPEED * dt;
    dy *= PLAYER_SPEED * dt;

    // Collision box: lower portion of sprite (feet area)
    float bx = g_player.x + 8;
    float by = g_player.y + 20;
    float bw = 16;
    float bh = 10;

    // Tilemap collision
    resolve_tilemap_collision(g_map, bx, by, bw, bh, dx, dy);

    // Decoration collision - X axis
    if (dx != 0 && check_decoration_collision(bx + dx, by, bw, bh))
        dx = 0;
    // Decoration collision - Y axis
    if (dy != 0 && check_decoration_collision(bx + dx, by + dy, bw, bh))
        dy = 0;

    // World bounds
    float new_x = g_player.x + dx;
    float new_y = g_player.y + dy;
    if (new_x < 0) new_x = 0;
    if (new_y < 0) new_y = 0;
    if (new_x > (MAP_W - 2) * TILE_SIZE) new_x = (float)((MAP_W - 2) * TILE_SIZE);
    if (new_y > (MAP_H - 2) * TILE_SIZE) new_y = (float)((MAP_H - 2) * TILE_SIZE);

    g_player.x = new_x;
    g_player.y = new_y;
    g_player.moving = moving;

    // Animation
    if (moving) {
        g_player.sprite.play(g_player.anim_walk[g_player.direction]);
    } else {
        g_player.sprite.play(g_player.anim_idle[g_player.direction]);
    }
    g_player.sprite.update(dt);

    // Invincibility timer
    if (g_player.hit_timer > 0)
        g_player.hit_timer -= dt;
}

static void update_enemies(float dt) {
    for (int i = 0; i < g_enemy_count; i++) {
        Enemy& e = g_enemies[i];
        if (!e.active) continue;

        // Simple patrol AI
        e.patrol_timer += dt;
        if (e.patrol_timer >= e.patrol_duration) {
            e.patrol_timer = 0;
            e.direction = (e.direction + 1) % 4;
            // Randomize duration slightly using frame count
            e.patrol_duration = 1.5f + (float)(g_engine.frame_count % 3) * 0.5f;
        }

        float dx = 0, dy = 0;
        switch (e.direction) {
            case DIR_DOWN:  dy =  ENEMY_SPEED * dt; break;
            case DIR_UP:    dy = -ENEMY_SPEED * dt; break;
            case DIR_RIGHT: dx =  ENEMY_SPEED * dt; break;
            case DIR_LEFT:  dx = -ENEMY_SPEED * dt; break;
        }

        // Tilemap collision
        float bx = e.x + 8;
        float by = e.y + 20;
        float bw = 16;
        float bh = 10;
        resolve_tilemap_collision(g_map, bx, by, bw, bh, dx, dy);

        // Reverse direction if blocked
        if (dx == 0 && dy == 0) {
            e.direction = (e.direction + 2) % 4;
            e.patrol_timer = 0;
        }

        // World bounds
        float new_x = e.x + dx;
        float new_y = e.y + dy;
        if (new_x < 0 || new_x > (MAP_W - 2) * TILE_SIZE) {
            e.direction = (e.direction + 2) % 4;
            e.patrol_timer = 0;
            new_x = e.x;
        }
        if (new_y < 0 || new_y > (MAP_H - 2) * TILE_SIZE) {
            e.direction = (e.direction + 2) % 4;
            e.patrol_timer = 0;
            new_y = e.y;
        }

        e.x = new_x;
        e.y = new_y;

        // Animation
        if (e.type == 0) {
            e.sprite.play(e.anim_walk[e.direction]);
        }
        e.sprite.update(dt);

        // Damage player on contact
        AABB pa = { g_player.x + 4, g_player.y + 8, 24, 22 };
        AABB ea = { e.x + 4, e.y + 8, 24, 22 };
        if (pa.overlaps(ea) && g_player.hit_timer <= 0) {
            g_player.health -= 10;
            g_player.hit_timer = 1.0f; // 1 second invincibility
            if (g_player.health < 0) g_player.health = 0;

            // Knockback with collision checking
            float kx = g_player.x - e.x;
            float ky = g_player.y - e.y;
            float len = 1.0f;
            if (kx != 0 || ky != 0) {
                float ax = kx < 0 ? -kx : kx;
                float ay = ky < 0 ? -ky : ky;
                len = ax > ay ? ax + ay * 0.4f : ay + ax * 0.4f;
            }
            float kb_dx = (kx / len) * 16.0f;
            float kb_dy = (ky / len) * 16.0f;

            // Check collision before applying knockback
            float bx = g_player.x + 8;
            float by = g_player.y + 20;
            resolve_tilemap_collision(g_map, bx, by, 16, 10, kb_dx, kb_dy);
            float new_px = g_player.x + kb_dx;
            float new_py = g_player.y + kb_dy;
            if (new_px < 0) new_px = 0;
            if (new_py < 0) new_py = 0;
            if (new_px > (MAP_W - 2) * TILE_SIZE) new_px = (float)((MAP_W - 2) * TILE_SIZE);
            if (new_py > (MAP_H - 2) * TILE_SIZE) new_py = (float)((MAP_H - 2) * TILE_SIZE);
            g_player.x = new_px;
            g_player.y = new_py;

            // Sound effect
            g_audio.play_tone(200, 80, 30);
        }
    }
}

static void update_interactions() {
    g_prompt[0] = '\0';

    // Check chest proximity
    for (int i = 0; i < g_chest_count; i++) {
        Chest& c = g_chests[i];
        if (c.collected) continue;

        float dx = g_player.x + 16 - (c.x + 8);
        float dy = g_player.y + 16 - (c.y + 8);
        float dist = dx * dx + dy * dy;

        if (dist < 40 * 40) {
            if (!c.opened) {
                snprintf(g_prompt, sizeof(g_prompt), "Press E to open chest");
                if (g_input.key_just_pressed(key::E)) {
                    c.opened = true;
                    c.collected = true;
                    g_player.score += 100;
                    snprintf(g_dialog_text, sizeof(g_dialog_text),
                             "You found treasure! +100 points");
                    g_dialog_timer = 2.0f;
                    g_audio.play_tone(523, 100, 25); // C5
                    g_audio.play_tone(659, 100, 25); // E5
                    g_audio.play_tone(784, 150, 25); // G5
                }
            }
        }
    }

    // Dialog timer
    if (g_dialog_timer > 0) {
        g_dialog_timer -= g_engine.dt;
        if (g_dialog_timer <= 0) g_dialog_text[0] = '\0';
    }
}

static void update(float dt) {
    if (g_player.health <= 0) {
        // Game over - respawn after pressing space
        if (g_input.key_just_pressed(key::SPACE)) {
            g_player.health = PLAYER_HP;
            g_player.x = 24 * TILE_SIZE;
            g_player.y = 22 * TILE_SIZE;
        }
        return;
    }

    update_player(dt);
    update_enemies(dt);
    update_interactions();
    update_camera();
}

// ============================================================================
// Rendering
// ============================================================================

// Y-sort helper for draw ordering
struct DrawEntry {
    float y;
    int type;   // 0=decoration, 1=player, 2=enemy, 3=chest
    int index;
};

static DrawEntry g_draw_list[MAX_DECORATIONS + MAX_ENEMIES + MAX_CHESTS + 1];
static int g_draw_count = 0;

static void sort_draw_list() {
    // Simple insertion sort (small N)
    for (int i = 1; i < g_draw_count; i++) {
        DrawEntry tmp = g_draw_list[i];
        int j = i - 1;
        while (j >= 0 && g_draw_list[j].y > tmp.y) {
            g_draw_list[j + 1] = g_draw_list[j];
            j--;
        }
        g_draw_list[j + 1] = tmp;
    }
}

static void render() {
    int cam_x = (int)g_cam_x;
    int cam_y = (int)g_cam_y;

    // Draw tilemap
    g_map.draw(g_engine.pixels, g_engine.screen_w, g_engine.screen_h,
               cam_x, cam_y, SCALE);

    // Build Y-sorted draw list for entities and decorations
    g_draw_count = 0;

    for (int i = 0; i < g_decor_count; i++) {
        Decoration& dc = g_decor[i];
        float h = 0;
        if (dc.sheet) {
            h = (dc.frame_col >= 0) ? (float)dc.sheet->frame_h
                                    : (float)dc.sheet->height;
        }
        g_draw_list[g_draw_count++] = { dc.y + h, 0, i };
    }

    for (int i = 0; i < g_chest_count; i++) {
        g_draw_list[g_draw_count++] = { g_chests[i].y + TILE_SIZE, 3, i };
    }

    g_draw_list[g_draw_count++] = { g_player.y + SPR_H, 1, 0 };

    for (int i = 0; i < g_enemy_count; i++) {
        if (!g_enemies[i].active) continue;
        float bottom = g_enemies[i].y + (g_enemies[i].type == 1 ? 64 : SPR_H);
        g_draw_list[g_draw_count++] = { bottom, 2, i };
    }

    sort_draw_list();

    // Render sorted entities
    for (int i = 0; i < g_draw_count; i++) {
        DrawEntry& de = g_draw_list[i];

        if (de.type == 0) {
            // Decoration
            Decoration& d = g_decor[de.index];
            if (d.sheet && d.sheet->pixels) {
                int sx = (int)(d.x * SCALE) - cam_x * SCALE;
                int sy = (int)(d.y * SCALE) - cam_y * SCALE;
                if (d.frame_col >= 0 && d.frame_row >= 0) {
                    d.sheet->draw_frame(g_engine.pixels, g_engine.screen_w,
                                        g_engine.screen_h, d.frame_col,
                                        d.frame_row, sx, sy, SCALE);
                } else {
                    d.sheet->draw_region(g_engine.pixels, g_engine.screen_w,
                                         g_engine.screen_h, 0, 0,
                                         d.sheet->width, d.sheet->height,
                                         sx, sy, SCALE);
                }
            }
        } else if (de.type == 1) {
            // Player
            int px = (int)(g_player.x * SCALE) - cam_x * SCALE;
            int py = (int)(g_player.y * SCALE) - cam_y * SCALE;

            // Flash when invincible
            bool visible = true;
            if (g_player.hit_timer > 0) {
                visible = ((int)(g_player.hit_timer * 10) % 2 == 0);
            }

            if (visible) {
                g_player.sprite.draw(g_engine.pixels, g_engine.screen_w,
                                     g_engine.screen_h, px, py, SCALE);
            }
        } else if (de.type == 2) {
            // Enemy
            Enemy& e = g_enemies[de.index];
            int ex = (int)(e.x * SCALE) - cam_x * SCALE;
            int ey = (int)(e.y * SCALE) - cam_y * SCALE;
            e.sprite.draw(g_engine.pixels, g_engine.screen_w,
                          g_engine.screen_h, ex, ey, SCALE);

            // Health bar above enemy
            if (e.health < e.max_health) {
                draw_bar(g_engine, ex + 4, ey - 8, 56, 6,
                         e.health, e.max_health,
                         0xFFCC3333, 0xFF333333, 0xFF000000);
            }
        } else if (de.type == 3) {
            // Chest
            Chest& c = g_chests[de.index];
            int cx = (int)(c.x * SCALE) - cam_x * SCALE;
            int cy = (int)(c.y * SCALE) - cam_y * SCALE;

            if (!c.collected && g_spr_chest.pixels) {
                // Draw chest (frame 0 = closed, use tinting for opened)
                g_spr_chest.draw_frame(g_engine.pixels, g_engine.screen_w,
                                       g_engine.screen_h, 0, 0,
                                       cx, cy, SCALE);
                if (c.opened) {
                    // Darken opened chests slightly
                    g_engine.fill_rect_alpha(cx, cy, TILE_SIZE * SCALE,
                                             TILE_SIZE * SCALE, 0x40000000);
                }
            }
        }
    }

    // ---- HUD ----

    // Health bar
    draw_bar(g_engine, 8, 8, 160, 20,
             g_player.health, g_player.max_health,
             0xFFCC3333, 0xFF444444, 0xFF000000);

    char hp_text[32];
    snprintf(hp_text, sizeof(hp_text), "HP: %d/%d", g_player.health, g_player.max_health);
    draw_hud_text(g_engine, 12, 9, hp_text,
                  Color::from_rgb(0xFF, 0xFF, 0xFF), 14);

    // Score
    char score_text[32];
    snprintf(score_text, sizeof(score_text), "Score: %d", g_player.score);
    draw_hud_text(g_engine, 8, 34, score_text,
                  Color::from_rgb(0xFF, 0xDD, 0x44), 14);

    // FPS counter
    if (g_engine.dt > 0) {
        char fps_text[16];
        int fps = (int)(1.0f / g_engine.dt);
        snprintf(fps_text, sizeof(fps_text), "%d FPS", fps);
        int fw = g_engine.text_width(fps_text, 12);
        draw_hud_text(g_engine, g_engine.screen_w - fw - 8, 8, fps_text,
                      Color::from_rgb(0xAA, 0xAA, 0xAA), 12);
    }

    // Interaction prompt
    if (g_prompt[0]) {
        draw_prompt(g_engine, g_prompt, 14);
    }

    // Dialog text
    if (g_dialog_text[0] && g_dialog_timer > 0) {
        int dw = 300;
        int dh = 50;
        int dx = (g_engine.screen_w - dw) / 2;
        int dy = g_engine.screen_h - 100;
        draw_dialog(g_engine, dx, dy, dw, dh, g_dialog_text,
                    Color::from_rgb(0x33, 0x33, 0x33), 14);
    }

    // Game over screen
    if (g_player.health <= 0) {
        g_engine.fill_rect_alpha(0, 0, g_engine.screen_w, g_engine.screen_h,
                                 0xAA000000);
        const char* go_text = "Game Over";
        int tw = g_engine.text_width(go_text, 32);
        g_engine.draw_text((g_engine.screen_w - tw) / 2,
                           g_engine.screen_h / 2 - 30,
                           go_text, Color::from_rgb(0xFF, 0x44, 0x44), 32);

        const char* restart_text = "Press SPACE to respawn";
        tw = g_engine.text_width(restart_text, 16);
        g_engine.draw_text((g_engine.screen_w - tw) / 2,
                           g_engine.screen_h / 2 + 10,
                           restart_text, Color::from_rgb(0xCC, 0xCC, 0xCC), 16);

        char final_score[64];
        snprintf(final_score, sizeof(final_score), "Final Score: %d", g_player.score);
        tw = g_engine.text_width(final_score, 18);
        g_engine.draw_text((g_engine.screen_w - tw) / 2,
                           g_engine.screen_h / 2 + 40,
                           final_score, Color::from_rgb(0xFF, 0xDD, 0x44), 18);
    }
}

// ============================================================================
// Entry point
// ============================================================================

extern "C" void _start() {
    // Initialize engine
    if (!g_engine.init("Montauk Quest", WIN_W, WIN_H)) {
        montauk::print("Failed to create game window\n");
        montauk::exit(1);
    }

    g_input.init();

    // Initialize audio (non-fatal if it fails)
    g_audio.init();

    // Load assets
    if (!load_assets()) {
        montauk::print("Failed to load game assets\n");
        g_engine.shutdown();
        montauk::exit(1);
    }

    // Initialize game world
    generate_world();
    init_player();

    // Main game loop
    while (g_engine.running) {
        g_engine.update_timing();
        g_input.begin_frame();

        // Process all pending events
        Montauk::WinEvent ev;
        while (g_engine.poll(&ev)) {
            g_input.handle_event(ev);

            // Escape to quit
            if (ev.type == 0 && ev.key.pressed && ev.key.scancode == key::ESC)
                g_engine.running = false;
        }

        // Update
        update(g_engine.dt);

        // Render
        render();
        g_engine.present();

        // Yield to avoid burning CPU when there's no input
        montauk::sleep_ms(1);
    }

    g_audio.shutdown();
    g_engine.shutdown();
    montauk::exit(0);
}
