#pragma once

#include <cstdint>

namespace novaria::world::material {

using MaterialId = std::uint16_t;

constexpr MaterialId kAir = 0;
constexpr MaterialId kDirt = 1;
constexpr MaterialId kStone = 2;
constexpr MaterialId kGrass = 3;
constexpr MaterialId kWater = 4;
constexpr MaterialId kWood = 5;
constexpr MaterialId kLeaves = 6;
constexpr MaterialId kCoalOre = 7;
constexpr MaterialId kTorch = 8;
constexpr MaterialId kWorkbench = 9;

struct RgbaColor final {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

enum class CollisionShape : std::uint8_t {
    Empty = 0,
    SolidFull = 1,
    SolidHalfLower = 2,
    SolidHalfUpper = 3,
    SolidSlopeUpRight = 4,
    SolidSlopeUpLeft = 5,
};

struct MaterialTraits final {
    MaterialId id = kAir;
    const char* debug_name = "air";
    bool is_solid = false;
    bool blocks_sunlight = false;
    bool harvestable_by_pickaxe = false;
    bool harvestable_by_axe = false;
    int harvest_ticks = 8;
    bool has_harvest_drop = false;
    MaterialId harvest_drop_material_id = kAir;
    RgbaColor base_color{};
    CollisionShape collision_shape = CollisionShape::Empty;
};

const MaterialTraits& Traits(MaterialId material_id);
CollisionShape CollisionShapeFor(MaterialId material_id);
bool IsSolid(MaterialId material_id);
bool BlocksSunlight(MaterialId material_id);
bool HasFloorSurface(MaterialId material_id);
bool IsSolidAt(MaterialId material_id, float local_x, float local_y);
float FloorSurfaceY(MaterialId material_id, float local_x);
float BottomSurfaceY(MaterialId material_id, float local_x);
bool IsHarvestableByPickaxe(MaterialId material_id);
bool IsHarvestableByAxe(MaterialId material_id);
bool IsHarvestableBySword(MaterialId material_id);
int HarvestTicks(MaterialId material_id);
bool TryResolveHarvestDrop(MaterialId material_id, MaterialId& out_drop_material_id);
RgbaColor BaseColor(MaterialId material_id);

}  // namespace novaria::world::material
