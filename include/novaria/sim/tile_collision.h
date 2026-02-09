#pragma once

#include "world/material_catalog.h"
#include "world/world_service.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace novaria::sim::collision {

struct Aabb final {
    float left = 0.0F;
    float right = 0.0F;
    float top = 0.0F;
    float bottom = 0.0F;
};

Aabb MakePlayerAabb(float center_x, float feet_y, float half_width, float height, float skin = 0.0F);

bool IntersectsTile(const Aabb& aabb, int tile_x, int tile_y);

bool WouldMaterialOverlapAabb(
    world::material::MaterialId material_id,
    int tile_x,
    int tile_y,
    const Aabb& aabb);

class TileCollisionSampler final {
public:
    explicit TileCollisionSampler(const world::IWorldService& world_service);

    world::material::MaterialId MaterialOrStone(int tile_x, int tile_y);
    bool IsSolidAtPoint(float world_x, float world_y);
    bool HasFloorSurfaceAt(int tile_x, int tile_y);
    float FloorSurfaceWorldY(int tile_x, int tile_y, float world_x);
    float BottomSurfaceWorldY(int tile_x, int tile_y, float world_x);

private:
    struct CacheEntry final {
        int tile_x = 0;
        int tile_y = 0;
        world::material::MaterialId material_id = world::material::kAir;
        bool valid = false;
    };

    static std::size_t HashTile(int tile_x, int tile_y);

    const world::IWorldService& world_service_;
    std::array<CacheEntry, 256> cache_{};
};

bool FindBestFloor(
    TileCollisionSampler& sampler,
    float left_x,
    float right_x,
    float reference_feet_y,
    float max_step_up,
    float max_step_down,
    float& out_floor_y);

bool IsAabbBlocked(
    TileCollisionSampler& sampler,
    float center_x,
    float feet_y,
    float half_width,
    float height);

}  // namespace novaria::sim::collision

