#include "sim/tile_collision.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace novaria::sim::collision {
namespace {

constexpr float kSampleEpsilon = 0.0005F;

}  // namespace

Aabb MakePlayerAabb(float center_x, float feet_y, float half_width, float height, float skin) {
    const float clamped_skin = std::max(0.0F, skin);
    return Aabb{
        .left = center_x - half_width + clamped_skin,
        .right = center_x + half_width - clamped_skin,
        .top = feet_y - height + clamped_skin,
        .bottom = feet_y - clamped_skin,
    };
}

bool IntersectsTile(const Aabb& aabb, int tile_x, int tile_y) {
    const float tile_left = static_cast<float>(tile_x);
    const float tile_right = static_cast<float>(tile_x + 1);
    const float tile_top = static_cast<float>(tile_y);
    const float tile_bottom = static_cast<float>(tile_y + 1);
    return aabb.left < tile_right &&
        aabb.right > tile_left &&
        aabb.top < tile_bottom &&
        aabb.bottom > tile_top;
}

bool WouldMaterialOverlapAabb(
    world::material::MaterialId material_id,
    int tile_x,
    int tile_y,
    const Aabb& aabb) {
    if (world::material::CollisionShapeFor(material_id) == world::material::CollisionShape::Empty) {
        return false;
    }
    if (!IntersectsTile(aabb, tile_x, tile_y)) {
        return false;
    }

    const float tile_left = static_cast<float>(tile_x);
    const float tile_right = static_cast<float>(tile_x + 1);
    const float tile_top = static_cast<float>(tile_y);
    const float tile_bottom = static_cast<float>(tile_y + 1);

    const float overlap_left = std::max(aabb.left, tile_left);
    const float overlap_right = std::min(aabb.right, tile_right);
    const float overlap_top = std::max(aabb.top, tile_top);
    const float overlap_bottom = std::min(aabb.bottom, tile_bottom);
    if (overlap_left >= overlap_right || overlap_top >= overlap_bottom) {
        return false;
    }

    const float mid_x = (overlap_left + overlap_right) * 0.5F;
    const float mid_y = (overlap_top + overlap_bottom) * 0.5F;
    const float xs[3] = {
        std::nextafter(overlap_left + kSampleEpsilon, overlap_right),
        mid_x,
        std::nextafter(overlap_right - kSampleEpsilon, overlap_left),
    };
    const float ys[3] = {
        std::nextafter(overlap_top + kSampleEpsilon, overlap_bottom),
        mid_y,
        std::nextafter(overlap_bottom - kSampleEpsilon, overlap_top),
    };

    for (float world_y : ys) {
        for (float world_x : xs) {
            const float local_x = std::clamp(world_x - tile_left, 0.0F, 1.0F);
            const float local_y = std::clamp(world_y - tile_top, 0.0F, 1.0F);
            if (world::material::IsSolidAt(material_id, local_x, local_y)) {
                return true;
            }
        }
    }

    return false;
}

TileCollisionSampler::TileCollisionSampler(const world::IWorldService& world_service)
    : world_service_(world_service) {}

std::size_t TileCollisionSampler::HashTile(int tile_x, int tile_y) {
    const std::uint32_t ux = static_cast<std::uint32_t>(tile_x);
    const std::uint32_t uy = static_cast<std::uint32_t>(tile_y);
    const std::uint32_t h =
        (ux * 73856093U) ^ (uy * 19349663U) ^ (ux >> 16) ^ (uy >> 16);
    return static_cast<std::size_t>(h);
}

world::material::MaterialId TileCollisionSampler::MaterialOrStone(int tile_x, int tile_y) {
    const std::size_t start = HashTile(tile_x, tile_y) % cache_.size();
    for (std::size_t probe = 0; probe < 4; ++probe) {
        CacheEntry& entry = cache_[(start + probe) % cache_.size()];
        if (entry.valid && entry.tile_x == tile_x && entry.tile_y == tile_y) {
            return entry.material_id;
        }
        if (!entry.valid) {
            std::uint16_t material_id = 0;
            if (!world_service_.TryReadTile(tile_x, tile_y, material_id)) {
                material_id = world::material::kStone;
            }
            entry = CacheEntry{
                .tile_x = tile_x,
                .tile_y = tile_y,
                .material_id = static_cast<world::material::MaterialId>(material_id),
                .valid = true,
            };
            return entry.material_id;
        }
    }

    std::uint16_t material_id = 0;
    if (!world_service_.TryReadTile(tile_x, tile_y, material_id)) {
        material_id = world::material::kStone;
    }
    cache_[start] = CacheEntry{
        .tile_x = tile_x,
        .tile_y = tile_y,
        .material_id = static_cast<world::material::MaterialId>(material_id),
        .valid = true,
    };
    return cache_[start].material_id;
}

bool TileCollisionSampler::IsSolidAtPoint(float world_x, float world_y) {
    const int tile_x = static_cast<int>(std::floor(world_x));
    const int tile_y = static_cast<int>(std::floor(world_y));
    const float local_x = world_x - static_cast<float>(tile_x);
    const float local_y = world_y - static_cast<float>(tile_y);
    const world::material::MaterialId material_id = MaterialOrStone(tile_x, tile_y);
    return world::material::IsSolidAt(material_id, local_x, local_y);
}

bool TileCollisionSampler::HasFloorSurfaceAt(int tile_x, int tile_y) {
    const world::material::MaterialId material_id = MaterialOrStone(tile_x, tile_y);
    return world::material::HasFloorSurface(material_id);
}

float TileCollisionSampler::FloorSurfaceWorldY(int tile_x, int tile_y, float world_x) {
    const float local_x = world_x - static_cast<float>(tile_x);
    const world::material::MaterialId material_id = MaterialOrStone(tile_x, tile_y);
    return static_cast<float>(tile_y) + world::material::FloorSurfaceY(material_id, local_x);
}

float TileCollisionSampler::BottomSurfaceWorldY(int tile_x, int tile_y, float world_x) {
    const float local_x = world_x - static_cast<float>(tile_x);
    const world::material::MaterialId material_id = MaterialOrStone(tile_x, tile_y);
    return static_cast<float>(tile_y) + world::material::BottomSurfaceY(material_id, local_x);
}

bool FindBestFloor(
    TileCollisionSampler& sampler,
    float left_x,
    float right_x,
    float reference_feet_y,
    float max_step_up,
    float max_step_down,
    float& out_floor_y) {
    float sample_left_x = left_x;
    float sample_right_x = right_x;
    if (sample_right_x < sample_left_x) {
        std::swap(sample_left_x, sample_right_x);
    }

    constexpr float kFootInset = 0.02F;
    const float inset_left = sample_left_x + kFootInset;
    const float inset_right = sample_right_x - kFootInset;
    if (inset_left <= inset_right) {
        sample_left_x = inset_left;
        sample_right_x = inset_right;
    } else {
        const float center = (sample_left_x + sample_right_x) * 0.5F;
        sample_left_x = center;
        sample_right_x = center;
    }

    const float sample_xs[3] = {
        sample_left_x,
        (sample_left_x + sample_right_x) * 0.5F,
        sample_right_x,
    };

    const int min_tile_y =
        static_cast<int>(std::floor(reference_feet_y - max_step_up)) - 1;
    const int max_tile_y =
        static_cast<int>(std::floor(reference_feet_y + max_step_down)) + 1;

    float best_floor = std::numeric_limits<float>::infinity();
    bool found = false;
    for (float sample_x : sample_xs) {
        const int tile_x = static_cast<int>(std::floor(sample_x));
        for (int tile_y = min_tile_y; tile_y <= max_tile_y; ++tile_y) {
            if (!sampler.HasFloorSurfaceAt(tile_x, tile_y)) {
                continue;
            }

            const float floor_y = sampler.FloorSurfaceWorldY(tile_x, tile_y, sample_x);
            const float delta = floor_y - reference_feet_y;
            if (delta < -max_step_up || delta > max_step_down) {
                continue;
            }
            if (floor_y < best_floor) {
                best_floor = floor_y;
                found = true;
            }
        }
    }

    if (found) {
        out_floor_y = best_floor;
    }
    return found;
}

bool IsAabbBlocked(
    TileCollisionSampler& sampler,
    float center_x,
    float feet_y,
    float half_width,
    float height) {
    const float left_x = center_x - half_width + 0.02F;
    const float right_x = center_x + half_width - 0.02F;
    const float top_y = feet_y - height + 0.02F;
    const float mid_y = feet_y - height * 0.5F;
    const float bottom_y = feet_y - 0.02F;
    return sampler.IsSolidAtPoint(left_x, top_y) ||
        sampler.IsSolidAtPoint(right_x, top_y) ||
        sampler.IsSolidAtPoint(left_x, mid_y) ||
        sampler.IsSolidAtPoint(right_x, mid_y) ||
        sampler.IsSolidAtPoint(left_x, bottom_y) ||
        sampler.IsSolidAtPoint(right_x, bottom_y);
}

}  // namespace novaria::sim::collision

