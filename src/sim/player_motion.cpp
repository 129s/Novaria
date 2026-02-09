#include "sim/player_motion.h"

#include "sim/tile_collision.h"

#include <algorithm>
#include <cmath>

namespace novaria::sim {
namespace {

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

constexpr float kEdgeInset = 0.02F;
constexpr float kCeilingProbeOffset = 0.001F;
constexpr float kHorizontalProbeOffset = 0.05F;
constexpr float kResolutionSkin = 0.0008F;

bool HasCeilingOverlapAtFeetY(
    collision::TileCollisionSampler& sampler,
    float center_x,
    float feet_y,
    float half_width,
    float height) {
    const float inset_x = std::max(0.0F, half_width - 0.03F);
    float left_x = center_x - inset_x;
    float right_x = center_x + inset_x;
    left_x = std::nextafter(left_x, center_x);
    right_x = std::nextafter(right_x, center_x);
    const float top_y = feet_y - height - kCeilingProbeOffset;
    return sampler.IsSolidAtPoint(left_x, top_y) ||
        sampler.IsSolidAtPoint(right_x, top_y) ||
        sampler.IsSolidAtPoint(center_x, top_y);
}

void ResolveHorizontalMovement(
    collision::TileCollisionSampler& sampler,
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

    if (!collision::IsAabbBlocked(sampler, target_x, state.position_y, half_width, height)) {
        state.position_x = target_x;
        return;
    }

    float stepped_floor = 0.0F;
    if (allow_step && collision::FindBestFloor(
            sampler,
            target_x - half_width,
            target_x + half_width,
            state.position_y,
            settings.step_height,
            settings.ground_snap,
            stepped_floor)) {
        if (!collision::IsAabbBlocked(sampler, target_x, stepped_floor, half_width, height)) {
            state.position_x = target_x;
            state.position_y = stepped_floor;
            state.on_ground = true;
            state.velocity_y = 0.0F;
            return;
        }
    }

    const bool moving_right = delta_x > 0.0F;
    const float probe_x = moving_right
        ? (target_x + half_width - kEdgeInset)
        : (target_x - half_width + kEdgeInset);

    const float head_y = state.position_y - height + kHorizontalProbeOffset;
    const float mid_y = state.position_y - height * 0.5F;
    const float foot_y = state.position_y - kHorizontalProbeOffset;
    const float probe_ys[3] = {head_y, mid_y, foot_y};

    float clamped_x = target_x;
    bool saw_collision = false;
    for (float probe_y : probe_ys) {
        if (!sampler.IsSolidAtPoint(probe_x, probe_y)) {
            continue;
        }

        saw_collision = true;
        const int tile_x = static_cast<int>(std::floor(probe_x));
        const float face_x = moving_right ? static_cast<float>(tile_x) : static_cast<float>(tile_x + 1);
        const float half_width_adjusted = (half_width - kEdgeInset);
        const float candidate_center_x = moving_right
            ? (face_x - half_width_adjusted - kResolutionSkin)
            : (face_x + half_width_adjusted + kResolutionSkin);
        if (moving_right) {
            clamped_x = std::min(clamped_x, candidate_center_x);
        } else {
            clamped_x = std::max(clamped_x, candidate_center_x);
        }
    }

    if (!saw_collision) {
        state.velocity_x = 0.0F;
        return;
    }

    if (moving_right) {
        clamped_x = std::max(from_x, std::min(clamped_x, target_x));
    } else {
        clamped_x = std::min(from_x, std::max(clamped_x, target_x));
    }

    if (!collision::IsAabbBlocked(sampler, clamped_x, state.position_y, half_width, height)) {
        state.position_x = clamped_x;
        state.velocity_x = 0.0F;
        return;
    }

    float ok_t = 0.0F;
    float bad_t = 1.0F;
    constexpr int kFallbackIterations = 6;
    for (int iter = 0; iter < kFallbackIterations; ++iter) {
        const float mid_t = (ok_t + bad_t) * 0.5F;
        const float mid_x = from_x + (target_x - from_x) * mid_t;
        if (!collision::IsAabbBlocked(sampler, mid_x, state.position_y, half_width, height)) {
            ok_t = mid_t;
        } else {
            bad_t = mid_t;
        }
    }

    state.position_x = from_x + (target_x - from_x) * ok_t;
    state.velocity_x = 0.0F;
}

void ResolveVerticalMovement(
    collision::TileCollisionSampler& sampler,
    const PlayerMotionSettings& settings,
    float delta_y,
    PlayerMotionState& state) {
    const float half_width = settings.half_width;
    const float height = settings.height;
    const float from_y = state.position_y;
    const float target_y = state.position_y + delta_y;

    if (delta_y < 0.0F) {
        if (!HasCeilingOverlapAtFeetY(sampler, state.position_x, target_y, half_width, height)) {
            state.position_y = target_y;
            return;
        }

        const float inset_x = std::max(0.0F, half_width - 0.03F);
        float left_x = state.position_x - inset_x;
        float right_x = state.position_x + inset_x;
        left_x = std::nextafter(left_x, state.position_x);
        right_x = std::nextafter(right_x, state.position_x);
        const float probe_xs[3] = {left_x, state.position_x, right_x};
        const float probe_top_y = target_y - height - kCeilingProbeOffset;

        float required_feet_y = target_y;
        for (float probe_x : probe_xs) {
            if (!sampler.IsSolidAtPoint(probe_x, probe_top_y)) {
                continue;
            }

            const int tile_x = static_cast<int>(std::floor(probe_x));
            const int tile_y = static_cast<int>(std::floor(probe_top_y));
            const float ceiling_bottom_world_y =
                sampler.BottomSurfaceWorldY(tile_x, tile_y, probe_x);
            required_feet_y =
                std::max(required_feet_y, ceiling_bottom_world_y + height + kCeilingProbeOffset + kResolutionSkin);
        }

        state.position_y = std::min(from_y, required_feet_y);
        state.velocity_y = 0.0F;
        state.on_ground = false;
        return;
    }

    const float max_down = std::max(delta_y, settings.ground_snap);
    float floor_y = 0.0F;
    if (collision::FindBestFloor(
            sampler,
            state.position_x - half_width,
            state.position_x + half_width,
            state.position_y,
            0.0F,
            max_down,
            floor_y)) {
        if (!collision::IsAabbBlocked(sampler, state.position_x, floor_y, half_width, height)) {
            state.position_y = floor_y;
            state.velocity_y = 0.0F;
            state.on_ground = true;
            return;
        }
    }

    state.position_y = target_y;
    state.on_ground = false;
}

void ResolvePenetrationIfNeeded(
    collision::TileCollisionSampler& sampler,
    const PlayerMotionSettings& settings,
    PlayerMotionState& state) {
    const float half_width = settings.half_width;
    const float height = settings.height;
    if (!collision::IsAabbBlocked(sampler, state.position_x, state.position_y, half_width, height)) {
        return;
    }

    const float start_y = state.position_y;
    constexpr float kStep = 0.05F;
    constexpr int kMaxSteps = 40;

    for (int i = 1; i <= kMaxSteps; ++i) {
        const float candidate_y = start_y - static_cast<float>(i) * kStep;
        if (!collision::IsAabbBlocked(sampler, state.position_x, candidate_y, half_width, height)) {
            state.position_y = candidate_y;
            state.velocity_y = 0.0F;
            state.on_ground = false;
            return;
        }
    }
    for (int i = 1; i <= kMaxSteps; ++i) {
        const float candidate_y = start_y + static_cast<float>(i) * kStep;
        if (!collision::IsAabbBlocked(sampler, state.position_x, candidate_y, half_width, height)) {
            state.position_y = candidate_y;
            state.velocity_y = 0.0F;
            state.on_ground = false;
            return;
        }
    }
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
    collision::TileCollisionSampler sampler(world_service);

    ResolvePenetrationIfNeeded(sampler, settings, state);

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

    ResolveHorizontalMovement(sampler, settings, delta_x, state);
    ResolveVerticalMovement(sampler, settings, delta_y, state);

    ResolvePenetrationIfNeeded(sampler, settings, state);
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
