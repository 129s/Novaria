#include "sim/player_motion.h"

#include "world/material_catalog.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace novaria::sim {
namespace {

struct SampleResult final {
    bool solid = false;
    int tile_x = 0;
    int tile_y = 0;
    std::uint16_t material_id = 0;
    float local_x = 0.0F;
    float local_y = 0.0F;
};

float ClampAxis(float value) {
    if (value < -1.0F) {
        return -1.0F;
    }
    if (value > 1.0F) {
        return 1.0F;
    }
    return value;
}

float Approach(float value, float target, float max_delta) {
    const float delta = target - value;
    if (std::fabs(delta) <= max_delta) {
        return target;
    }
    return value + (delta > 0.0F ? max_delta : -max_delta);
}

bool SampleTile(
    const world::IWorldService& world_service,
    float world_x,
    float world_y,
    SampleResult& out_sample) {
    const int tile_x = static_cast<int>(std::floor(world_x));
    const int tile_y = static_cast<int>(std::floor(world_y));
    std::uint16_t material_id = 0;
    const bool has_tile = world_service.TryReadTile(tile_x, tile_y, material_id);
    if (!has_tile) {
        material_id = world::material::kStone;
    }

    const float local_x = world_x - static_cast<float>(tile_x);
    const float local_y = world_y - static_cast<float>(tile_y);
    out_sample = SampleResult{
        .solid = world::material::IsSolidAt(material_id, local_x, local_y),
        .tile_x = tile_x,
        .tile_y = tile_y,
        .material_id = material_id,
        .local_x = local_x,
        .local_y = local_y,
    };
    return out_sample.solid;
}

bool IsSolidAtPoint(
    const world::IWorldService& world_service,
    float world_x,
    float world_y) {
    SampleResult sample{};
    return SampleTile(world_service, world_x, world_y, sample);
}

bool HasCeilingCollision(
    const world::IWorldService& world_service,
    float center_x,
    float top_y,
    float half_width) {
    const float inset = half_width * 0.6F;
    return IsSolidAtPoint(world_service, center_x - inset, top_y) ||
        IsSolidAtPoint(world_service, center_x + inset, top_y) ||
        IsSolidAtPoint(world_service, center_x, top_y);
}

bool HasCeilingOverlapAtFeetY(
    const world::IWorldService& world_service,
    float center_x,
    float feet_y,
    float half_width,
    float height) {
    const float inset_x = std::max(0.0F, half_width - 0.03F);
    float left_x = center_x - inset_x;
    float right_x = center_x + inset_x;
    left_x = std::nextafter(left_x, center_x);
    right_x = std::nextafter(right_x, center_x);
    const float top_y = feet_y - height - 0.001F;
    return IsSolidAtPoint(world_service, left_x, top_y) ||
        IsSolidAtPoint(world_service, right_x, top_y) ||
        IsSolidAtPoint(world_service, center_x, top_y);
}

bool HasHorizontalCollision(
    const world::IWorldService& world_service,
    float edge_x,
    float feet_y,
    float height) {
    const float head_y = feet_y - height + 0.05F;
    const float mid_y = feet_y - height * 0.5F;
    const float foot_y = feet_y - 0.05F;
    return IsSolidAtPoint(world_service, edge_x, head_y) ||
        IsSolidAtPoint(world_service, edge_x, mid_y) ||
        IsSolidAtPoint(world_service, edge_x, foot_y);
}

bool FindBestFloor(
    const world::IWorldService& world_service,
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
    const int base_tile_y = static_cast<int>(std::floor(reference_feet_y));
    float best_floor = std::numeric_limits<float>::infinity();
    bool found = false;
    for (float sample_x : sample_xs) {
        const int tile_x = static_cast<int>(std::floor(sample_x));
        const float local_x = sample_x - static_cast<float>(tile_x);
        for (int tile_y = base_tile_y - 2; tile_y <= base_tile_y + 2; ++tile_y) {
            std::uint16_t material_id = 0;
            const bool has_tile = world_service.TryReadTile(tile_x, tile_y, material_id);
            if (!has_tile) {
                material_id = world::material::kStone;
            }
            if (!world::material::HasFloorSurface(material_id)) {
                continue;
            }
            const float surface_y =
                world::material::FloorSurfaceY(material_id, local_x);
            const float floor_y = static_cast<float>(tile_y) + surface_y;
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
    const world::IWorldService& world_service,
    float center_x,
    float feet_y,
    float half_width,
    float height) {
    const float left_x = center_x - half_width + 0.02F;
    const float right_x = center_x + half_width - 0.02F;
    const float top_y = feet_y - height + 0.02F;
    const float mid_y = feet_y - height * 0.5F;
    const float bottom_y = feet_y - 0.02F;
    return IsSolidAtPoint(world_service, left_x, top_y) ||
        IsSolidAtPoint(world_service, right_x, top_y) ||
        IsSolidAtPoint(world_service, left_x, mid_y) ||
        IsSolidAtPoint(world_service, right_x, mid_y) ||
        IsSolidAtPoint(world_service, left_x, bottom_y) ||
        IsSolidAtPoint(world_service, right_x, bottom_y);
}

void ResolveHorizontalMovement(
    const world::IWorldService& world_service,
    const PlayerMotionSettings& settings,
    float delta_x,
    PlayerMotionState& state) {
    if (std::fabs(delta_x) < 0.0001F) {
        return;
    }

    const bool allow_step = state.on_ground && state.velocity_y >= 0.0F;
    const float half_width = settings.half_width;
    const float height = settings.height;
    const float from_x = state.position_x;
    const float target_x = state.position_x + delta_x;
    if (!IsAabbBlocked(world_service, target_x, state.position_y, half_width, height)) {
        state.position_x = target_x;
        return;
    }

    float stepped_floor = 0.0F;
    if (allow_step && FindBestFloor(
            world_service,
            target_x - half_width,
            target_x + half_width,
            state.position_y,
            settings.step_height,
            settings.ground_snap,
            stepped_floor)) {
        if (!IsAabbBlocked(world_service, target_x, stepped_floor, half_width, height)) {
            state.position_x = target_x;
            state.position_y = stepped_floor;
            state.on_ground = true;
            state.velocity_y = 0.0F;
            return;
        }
    }

    float ok_t = 0.0F;
    float bad_t = 1.0F;
    constexpr int kSweepIterations = 10;
    for (int iter = 0; iter < kSweepIterations; ++iter) {
        const float mid_t = (ok_t + bad_t) * 0.5F;
        const float mid_x = from_x + (target_x - from_x) * mid_t;
        if (!IsAabbBlocked(world_service, mid_x, state.position_y, half_width, height)) {
            ok_t = mid_t;
        } else {
            bad_t = mid_t;
        }
    }

    state.position_x = from_x + (target_x - from_x) * ok_t;
    state.velocity_x = 0.0F;
}

void ResolveVerticalMovement(
    const world::IWorldService& world_service,
    const PlayerMotionSettings& settings,
    float delta_y,
    PlayerMotionState& state) {
    const float half_width = settings.half_width;
    const float height = settings.height;
    const float from_y = state.position_y;
    const float target_y = state.position_y + delta_y;

    if (delta_y < 0.0F) {
        if (!HasCeilingOverlapAtFeetY(
                world_service,
                state.position_x,
                target_y,
                half_width,
                height)) {
            state.position_y = target_y;
            return;
        }

        float ok_t = 0.0F;
        float bad_t = 1.0F;
        constexpr int kSweepIterations = 10;
        for (int iter = 0; iter < kSweepIterations; ++iter) {
            const float mid_t = (ok_t + bad_t) * 0.5F;
            const float mid_y = from_y + (target_y - from_y) * mid_t;
            if (!HasCeilingOverlapAtFeetY(
                    world_service,
                    state.position_x,
                    mid_y,
                    half_width,
                    height)) {
                ok_t = mid_t;
            } else {
                bad_t = mid_t;
            }
        }

        state.position_y = from_y + (target_y - from_y) * ok_t;
        state.velocity_y = 0.0F;
        state.on_ground = false;
        return;
    }

    const float max_down = std::max(delta_y, settings.ground_snap);
    float floor_y = 0.0F;
    if (FindBestFloor(
            world_service,
            state.position_x - half_width,
            state.position_x + half_width,
            state.position_y,
            0.0F,
            max_down,
            floor_y)) {
        if (!IsAabbBlocked(world_service, state.position_x, floor_y, half_width, height)) {
            state.position_y = std::max(state.position_y, floor_y);
            state.velocity_y = 0.0F;
            state.on_ground = true;
            return;
        }
    }

    state.position_y = target_y;
    state.on_ground = false;
}

}  // namespace

const PlayerMotionSettings& DefaultPlayerMotionSettings() {
    static const PlayerMotionSettings kSettings{};
    return kSettings;
}

void ResetPlayerMotionState(PlayerMotionState& state) {
    state = PlayerMotionState{};
}

void UpdatePlayerMotion(
    const PlayerMotionInput& input,
    const PlayerMotionSettings& settings,
    const world::IWorldService& world_service,
    double fixed_delta_seconds,
    PlayerMotionState& state) {
    const float dt = static_cast<float>(fixed_delta_seconds);
    const float clamped_axis = ClampAxis(input.move_axis);
    const float target_speed = clamped_axis * settings.max_speed;
    const float accel =
        std::fabs(target_speed) > 0.01F ? settings.acceleration : settings.deceleration;
    state.velocity_x = Approach(state.velocity_x, target_speed, accel * dt);

    if (input.jump_pressed && state.on_ground) {
        state.velocity_y = -settings.jump_speed;
        state.on_ground = false;
    }

    state.velocity_y = std::min(
        state.velocity_y + settings.gravity * dt,
        settings.max_fall_speed);

    const float delta_x = state.velocity_x * dt;
    const float delta_y = state.velocity_y * dt;

    ResolveHorizontalMovement(world_service, settings, delta_x, state);
    ResolveVerticalMovement(world_service, settings, delta_y, state);
}

PlayerMotionSnapshot SnapshotPlayerMotion(const PlayerMotionState& state) {
    return PlayerMotionSnapshot{
        .position_x = state.position_x,
        .position_y = state.position_y,
        .velocity_x = state.velocity_x,
        .velocity_y = state.velocity_y,
        .on_ground = state.on_ground,
    };
}

}  // namespace novaria::sim
