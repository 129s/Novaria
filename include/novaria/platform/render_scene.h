#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace novaria::platform {

struct RgbaColor final {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

struct RenderTile final {
    int world_tile_x = 0;
    int world_tile_y = 0;
    std::uint8_t light_level = 255;
    RgbaColor color{};
};

enum class RenderLayer : std::uint8_t {
    WorldOverlay = 0,
    UI = 1,
    Debug = 2,
};

enum class RenderCommandKind : std::uint8_t {
    FilledRect = 0,
    Line = 1,
    Text = 2,
};

struct RenderFilledRect final {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

struct RenderLine final {
    float x1 = 0.0F;
    float y1 = 0.0F;
    float x2 = 0.0F;
    float y2 = 0.0F;
};

struct RenderText final {
    float x = 0.0F;
    float y = 0.0F;
    float scale = 2.0F;
    std::string text;
};

struct RenderCommand final {
    RenderLayer layer = RenderLayer::UI;
    int z = 0;
    RenderCommandKind kind = RenderCommandKind::FilledRect;
    RgbaColor color{};
    RenderFilledRect filled_rect{};
    RenderLine line{};
    RenderText text{};

    static RenderCommand FilledRect(
        RenderLayer layer_in,
        int z_in,
        float x_in,
        float y_in,
        float width_in,
        float height_in,
        RgbaColor color_in) {
        RenderCommand command{};
        command.layer = layer_in;
        command.z = z_in;
        command.kind = RenderCommandKind::FilledRect;
        command.color = color_in;
        command.filled_rect = RenderFilledRect{
            .x = x_in,
            .y = y_in,
            .width = width_in,
            .height = height_in,
        };
        return command;
    }

    static RenderCommand Line(
        RenderLayer layer_in,
        int z_in,
        float x1_in,
        float y1_in,
        float x2_in,
        float y2_in,
        RgbaColor color_in) {
        RenderCommand command{};
        command.layer = layer_in;
        command.z = z_in;
        command.kind = RenderCommandKind::Line;
        command.color = color_in;
        command.line = RenderLine{
            .x1 = x1_in,
            .y1 = y1_in,
            .x2 = x2_in,
            .y2 = y2_in,
        };
        return command;
    }

    static RenderCommand Text(
        RenderLayer layer_in,
        int z_in,
        float x_in,
        float y_in,
        float scale_in,
        std::string text_in,
        RgbaColor color_in) {
        RenderCommand command{};
        command.layer = layer_in;
        command.z = z_in;
        command.kind = RenderCommandKind::Text;
        command.color = color_in;
        command.text = RenderText{
            .x = x_in,
            .y = y_in,
            .scale = scale_in,
            .text = std::move(text_in),
        };
        return command;
    }
};

struct RenderScene final {
    float camera_tile_x = 0.0F;
    float camera_tile_y = 0.0F;
    int view_tiles_x = 0;
    int view_tiles_y = 0;
    int tile_pixel_size = 32;
    float daylight_factor = 1.0F;
    std::vector<RenderTile> tiles;
    std::vector<RenderCommand> overlay_commands;
};

}  // namespace novaria::platform
