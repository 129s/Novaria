#pragma once

#include "world/world_service.h"

namespace novaria::sim {

struct PlayerMotionInput final {
    float move_axis = 0.0F;
    bool jump_pressed = false;
};

struct PlayerMotionState final {
    float position_x = 0.0F;
    float position_y = -2.0F;
    float velocity_x = 0.0F;
    float velocity_y = 0.0F;
    bool on_ground = false;
};

struct PlayerMotionSnapshot final {
    float position_x = 0.0F;
    float position_y = 0.0F;
    float velocity_x = 0.0F;
    float velocity_y = 0.0F;
    bool on_ground = false;
};

struct PlayerMotionSettings final {
    float max_speed = 3.6F;
    float acceleration = 18.0F;
    float deceleration = 24.0F;
    float gravity = 20.0F;
    float jump_speed = 7.2F;
    float max_fall_speed = 12.0F;
    float half_width = 0.35F;
    float height = 0.85F;
    float step_height = 0.35F;
    float ground_snap = 0.05F;
};

const PlayerMotionSettings& DefaultPlayerMotionSettings();

void ResetPlayerMotionState(PlayerMotionState& state);

void UpdatePlayerMotion(
    const PlayerMotionInput& input,
    const PlayerMotionSettings& settings,
    const world::IWorldService& world_service,
    double fixed_delta_seconds,
    PlayerMotionState& state);

PlayerMotionSnapshot SnapshotPlayerMotion(const PlayerMotionState& state);

}  // namespace novaria::sim
