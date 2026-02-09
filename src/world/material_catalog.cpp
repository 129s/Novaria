#include "world/material_catalog.h"

#include <algorithm>
#include <array>

namespace novaria::world::material {
namespace {

constexpr MaterialTraits kUnknownTraits{};

constexpr std::array<MaterialTraits, 10> kTraitsById = {
    MaterialTraits{
        .id = kAir,
        .debug_name = "air",
        .is_solid = false,
        .blocks_sunlight = false,
        .harvestable_by_pickaxe = false,
        .harvestable_by_axe = false,
        .harvest_ticks = 0,
        .has_harvest_drop = false,
        .harvest_drop_material_id = kAir,
        .base_color = RgbaColor{.r = 0, .g = 0, .b = 0, .a = 0},
        .collision_shape = CollisionShape::Empty,
    },
    MaterialTraits{
        .id = kDirt,
        .debug_name = "dirt",
        .is_solid = true,
        .blocks_sunlight = true,
        .harvestable_by_pickaxe = true,
        .harvestable_by_axe = false,
        .harvest_ticks = 8,
        .has_harvest_drop = true,
        .harvest_drop_material_id = kDirt,
        .base_color = RgbaColor{.r = 126, .g = 88, .b = 50, .a = 255},
        .collision_shape = CollisionShape::SolidFull,
    },
    MaterialTraits{
        .id = kStone,
        .debug_name = "stone",
        .is_solid = true,
        .blocks_sunlight = true,
        .harvestable_by_pickaxe = true,
        .harvestable_by_axe = false,
        .harvest_ticks = 18,
        .has_harvest_drop = true,
        .harvest_drop_material_id = kStone,
        .base_color = RgbaColor{.r = 116, .g = 122, .b = 132, .a = 255},
        .collision_shape = CollisionShape::SolidFull,
    },
    MaterialTraits{
        .id = kGrass,
        .debug_name = "grass",
        .is_solid = true,
        .blocks_sunlight = true,
        .harvestable_by_pickaxe = true,
        .harvestable_by_axe = false,
        .harvest_ticks = 8,
        .has_harvest_drop = true,
        .harvest_drop_material_id = kDirt,
        .base_color = RgbaColor{.r = 82, .g = 160, .b = 58, .a = 255},
        .collision_shape = CollisionShape::SolidFull,
    },
    MaterialTraits{
        .id = kWater,
        .debug_name = "water",
        .is_solid = false,
        .blocks_sunlight = false,
        .harvestable_by_pickaxe = false,
        .harvestable_by_axe = false,
        .harvest_ticks = 0,
        .has_harvest_drop = false,
        .harvest_drop_material_id = kAir,
        .base_color = RgbaColor{.r = 58, .g = 124, .b = 206, .a = 190},
        .collision_shape = CollisionShape::Empty,
    },
    MaterialTraits{
        .id = kWood,
        .debug_name = "wood",
        .is_solid = true,
        .blocks_sunlight = true,
        .harvestable_by_pickaxe = false,
        .harvestable_by_axe = true,
        .harvest_ticks = 12,
        .has_harvest_drop = true,
        .harvest_drop_material_id = kWood,
        .base_color = RgbaColor{.r = 124, .g = 84, .b = 44, .a = 255},
        .collision_shape = CollisionShape::SolidFull,
    },
    MaterialTraits{
        .id = kLeaves,
        .debug_name = "leaves",
        .is_solid = false,
        .blocks_sunlight = false,
        .harvestable_by_pickaxe = false,
        .harvestable_by_axe = true,
        .harvest_ticks = 6,
        .has_harvest_drop = false,
        .harvest_drop_material_id = kAir,
        .base_color = RgbaColor{.r = 64, .g = 128, .b = 52, .a = 235},
        .collision_shape = CollisionShape::Empty,
    },
    MaterialTraits{
        .id = kCoalOre,
        .debug_name = "coal_ore",
        .is_solid = true,
        .blocks_sunlight = true,
        .harvestable_by_pickaxe = true,
        .harvestable_by_axe = false,
        .harvest_ticks = 20,
        .has_harvest_drop = true,
        .harvest_drop_material_id = kCoalOre,
        .base_color = RgbaColor{.r = 58, .g = 60, .b = 66, .a = 255},
        .collision_shape = CollisionShape::SolidFull,
    },
    MaterialTraits{
        .id = kTorch,
        .debug_name = "torch",
        .is_solid = false,
        .blocks_sunlight = false,
        .harvestable_by_pickaxe = true,
        .harvestable_by_axe = false,
        .harvest_ticks = 4,
        .has_harvest_drop = true,
        .harvest_drop_material_id = kTorch,
        .base_color = RgbaColor{.r = 244, .g = 184, .b = 54, .a = 255},
        .collision_shape = CollisionShape::Empty,
    },
    MaterialTraits{
        .id = kWorkbench,
        .debug_name = "workbench",
        .is_solid = true,
        .blocks_sunlight = true,
        .harvestable_by_pickaxe = false,
        .harvestable_by_axe = false,
        .harvest_ticks = 8,
        .has_harvest_drop = false,
        .harvest_drop_material_id = kAir,
        .base_color = RgbaColor{.r = 164, .g = 118, .b = 70, .a = 255},
        .collision_shape = CollisionShape::SolidFull,
    },
};

}  // namespace

const MaterialTraits& Traits(MaterialId material_id) {
    if (material_id < kTraitsById.size()) {
        return kTraitsById[material_id];
    }

    return kUnknownTraits;
}

CollisionShape CollisionShapeFor(MaterialId material_id) {
    return Traits(material_id).collision_shape;
}

bool IsSolid(MaterialId material_id) {
    return CollisionShapeFor(material_id) != CollisionShape::Empty;
}

bool BlocksSunlight(MaterialId material_id) {
    return Traits(material_id).blocks_sunlight;
}

bool HasFloorSurface(MaterialId material_id) {
    switch (CollisionShapeFor(material_id)) {
        case CollisionShape::SolidFull:
        case CollisionShape::SolidHalfLower:
        case CollisionShape::SolidSlopeUpRight:
        case CollisionShape::SolidSlopeUpLeft:
            return true;
        case CollisionShape::Empty:
        case CollisionShape::SolidHalfUpper:
            return false;
    }

    return false;
}

float FloorSurfaceY(MaterialId material_id, float local_x) {
    const float clamped_x = std::clamp(local_x, 0.0F, 1.0F);
    switch (CollisionShapeFor(material_id)) {
        case CollisionShape::SolidFull:
            return 0.0F;
        case CollisionShape::SolidHalfLower:
            return 0.5F;
        case CollisionShape::SolidSlopeUpRight:
            return 1.0F - clamped_x;
        case CollisionShape::SolidSlopeUpLeft:
            return clamped_x;
        case CollisionShape::SolidHalfUpper:
        case CollisionShape::Empty:
            return 1.0F;
    }

    return 1.0F;
}

float BottomSurfaceY(MaterialId material_id, float local_x) {
    (void)local_x;
    switch (CollisionShapeFor(material_id)) {
        case CollisionShape::SolidFull:
        case CollisionShape::SolidHalfLower:
        case CollisionShape::SolidSlopeUpRight:
        case CollisionShape::SolidSlopeUpLeft:
            return 1.0F;
        case CollisionShape::SolidHalfUpper:
            return 0.5F;
        case CollisionShape::Empty:
            return 0.0F;
    }

    return 0.0F;
}

bool IsSolidAt(MaterialId material_id, float local_x, float local_y) {
    const float clamped_x = std::clamp(local_x, 0.0F, 1.0F);
    const float clamped_y = std::clamp(local_y, 0.0F, 1.0F);
    switch (CollisionShapeFor(material_id)) {
        case CollisionShape::Empty:
            return false;
        case CollisionShape::SolidFull:
            return true;
        case CollisionShape::SolidHalfLower:
            return clamped_y >= 0.5F;
        case CollisionShape::SolidHalfUpper:
            return clamped_y <= 0.5F;
        case CollisionShape::SolidSlopeUpRight:
            return clamped_y >= (1.0F - clamped_x);
        case CollisionShape::SolidSlopeUpLeft:
            return clamped_y >= clamped_x;
    }

    return false;
}

bool IsHarvestableByPickaxe(MaterialId material_id) {
    return Traits(material_id).harvestable_by_pickaxe;
}

bool IsHarvestableByAxe(MaterialId material_id) {
    return Traits(material_id).harvestable_by_axe;
}

bool IsHarvestableBySword(MaterialId material_id) {
    const MaterialTraits& traits = Traits(material_id);
    return traits.harvestable_by_pickaxe || traits.harvestable_by_axe;
}

int HarvestTicks(MaterialId material_id) {
    return Traits(material_id).harvest_ticks;
}

bool TryResolveHarvestDrop(MaterialId material_id, MaterialId& out_drop_material_id) {
    const MaterialTraits& traits = Traits(material_id);
    if (!traits.has_harvest_drop) {
        return false;
    }

    out_drop_material_id = traits.harvest_drop_material_id;
    return true;
}

RgbaColor BaseColor(MaterialId material_id) {
    return Traits(material_id).base_color;
}

}  // namespace novaria::world::material
