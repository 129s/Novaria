#include "platform/sdl_context.h"

#include "core/logger.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace novaria::platform {
namespace {

std::string SdlError() {
    const char* error = SDL_GetError();
    return error == nullptr ? std::string("unknown error") : std::string(error);
}

bool IsQuitEvent(Uint32 event_type) {
    return event_type == SDL_EVENT_QUIT ||
        event_type == SDL_EVENT_WINDOW_CLOSE_REQUESTED;
}

struct RgbaColor final {
    Uint8 r = 0;
    Uint8 g = 0;
    Uint8 b = 0;
    Uint8 a = 255;
};

Uint8 ClampToByte(int value) {
    return static_cast<Uint8>(std::clamp(value, 0, 255));
}

RgbaColor MaterialColor(std::uint16_t material_id) {
    switch (material_id) {
        case 1:
            return RgbaColor{
                .r = 126,
                .g = 88,
                .b = 50,
                .a = 255,
            };
        case 2:
            return RgbaColor{
                .r = 116,
                .g = 122,
                .b = 132,
                .a = 255,
            };
        default:
            return RgbaColor{};
    }
}

RgbaColor ApplyTileVariation(
    const RgbaColor& base_color,
    int world_tile_x,
    int world_tile_y) {
    const int shade_delta = ((world_tile_x + world_tile_y) & 1) == 0 ? 8 : -8;
    return RgbaColor{
        .r = ClampToByte(static_cast<int>(base_color.r) + shade_delta),
        .g = ClampToByte(static_cast<int>(base_color.g) + shade_delta),
        .b = ClampToByte(static_cast<int>(base_color.b) + shade_delta),
        .a = base_color.a,
    };
}

void DrawFilledRect(
    SDL_Renderer* renderer,
    int x,
    int y,
    int width,
    int height,
    const RgbaColor& color) {
    if (renderer == nullptr || width <= 0 || height <= 0) {
        return;
    }

    (void)SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    const SDL_FRect rect{
        static_cast<float>(x),
        static_cast<float>(y),
        static_cast<float>(width),
        static_cast<float>(height),
    };
    (void)SDL_RenderFillRect(renderer, &rect);
}

}  // namespace

SdlContext::~SdlContext() {
    Shutdown();
}

bool SdlContext::Initialize(const core::GameConfig& config) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        core::Logger::Error("platform", "SDL_Init failed: " + SdlError());
        return false;
    }

    window_ = SDL_CreateWindow(
        config.window_title.c_str(),
        config.window_width,
        config.window_height,
        SDL_WINDOW_RESIZABLE);
    if (window_ == nullptr) {
        core::Logger::Error("platform", "SDL_CreateWindow failed: " + SdlError());
        SDL_Quit();
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (renderer_ == nullptr) {
        core::Logger::Error("platform", "SDL_CreateRenderer failed: " + SdlError());
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        SDL_Quit();
        return false;
    }

    const int vsync = config.vsync ? 1 : 0;
    (void)SDL_SetRenderVSync(renderer_, vsync);

    core::Logger::Info("platform", "SDL3 context initialized.");
    return true;
}

bool SdlContext::PumpEvents(bool& quit_requested, InputActions& out_actions) {
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        if (IsQuitEvent(event.type)) {
            quit_requested = true;
        }

        if (event.type == SDL_EVENT_KEY_DOWN) {
            if (event.key.scancode == SDL_SCANCODE_E) {
                out_actions.player_mine = true;
            }

            if (event.key.scancode == SDL_SCANCODE_R) {
                out_actions.player_place = true;
            }

            if (event.key.scancode == SDL_SCANCODE_1) {
                out_actions.select_material_dirt = true;
            }

            if (event.key.scancode == SDL_SCANCODE_2) {
                out_actions.select_material_stone = true;
            }

            if (event.key.scancode == SDL_SCANCODE_J) {
                out_actions.send_jump_command = true;
            }

            if (event.key.scancode == SDL_SCANCODE_K) {
                out_actions.send_attack_command = true;
            }

            if (event.key.scancode == SDL_SCANCODE_F1) {
                out_actions.emit_script_ping = true;
            }

            if (event.key.scancode == SDL_SCANCODE_F2) {
                out_actions.debug_set_tile_air = true;
            }

            if (event.key.scancode == SDL_SCANCODE_F3) {
                out_actions.debug_set_tile_stone = true;
            }

            if (event.key.scancode == SDL_SCANCODE_F4) {
                out_actions.debug_net_disconnect = true;
            }

            if (event.key.scancode == SDL_SCANCODE_F5) {
                out_actions.debug_net_heartbeat = true;
            }

            if (event.key.scancode == SDL_SCANCODE_F6) {
                out_actions.debug_net_connect = true;
            }

            if (event.key.scancode == SDL_SCANCODE_F7) {
                out_actions.gameplay_collect_wood = true;
            }

            if (event.key.scancode == SDL_SCANCODE_F8) {
                out_actions.gameplay_collect_stone = true;
            }

            if (event.key.scancode == SDL_SCANCODE_F9) {
                out_actions.gameplay_build_workbench = true;
            }

            if (event.key.scancode == SDL_SCANCODE_F10) {
                out_actions.gameplay_craft_sword = true;
            }

            if (event.key.scancode == SDL_SCANCODE_F11) {
                out_actions.gameplay_attack_enemy = true;
            }

            if (event.key.scancode == SDL_SCANCODE_F12) {
                out_actions.gameplay_attack_boss = true;
            }
        }
    }

    SDL_PumpEvents();
    const bool* keyboard_state = SDL_GetKeyboardState(nullptr);
    if (keyboard_state != nullptr) {
        const bool move_left_wasd = keyboard_state[SDL_SCANCODE_A];
        const bool move_right_wasd = keyboard_state[SDL_SCANCODE_D];
        const bool move_up_wasd = keyboard_state[SDL_SCANCODE_W];
        const bool move_down_wasd = keyboard_state[SDL_SCANCODE_S];
        bool move_left_arrow = false;
        bool move_right_arrow = false;
        bool move_up_arrow = false;
        bool move_down_arrow = false;
        move_left_arrow = keyboard_state[SDL_SCANCODE_LEFT];
        move_right_arrow = keyboard_state[SDL_SCANCODE_RIGHT];
        move_up_arrow = keyboard_state[SDL_SCANCODE_UP];
        move_down_arrow = keyboard_state[SDL_SCANCODE_DOWN];
        out_actions.move_left = move_left_wasd || move_left_arrow;
        out_actions.move_right = move_right_wasd || move_right_arrow;
        out_actions.move_up = move_up_wasd || move_up_arrow;
        out_actions.move_down = move_down_wasd || move_down_arrow;
    }

    return true;
}

void SdlContext::RenderFrame(float interpolation_alpha, const RenderScene& scene) {
    if (renderer_ == nullptr) {
        return;
    }
    (void)interpolation_alpha;
    (void)SDL_SetRenderDrawColor(renderer_, 14, 24, 36, 255);
    (void)SDL_RenderClear(renderer_);

    int window_width = 0;
    int window_height = 0;
    if (window_ != nullptr) {
        (void)SDL_GetWindowSize(window_, &window_width, &window_height);
    }
    if (window_width <= 0 || window_height <= 0) {
        SDL_RenderPresent(renderer_);
        return;
    }

    const int tile_pixel_size = std::max(scene.tile_pixel_size, 8);
    const int view_tiles_x = std::max(scene.view_tiles_x, 1);
    const int view_tiles_y = std::max(scene.view_tiles_y, 1);
    const int half_tiles_x = view_tiles_x / 2;
    const int half_tiles_y = view_tiles_y / 2;
    const int first_world_tile_x = scene.camera_tile_x - half_tiles_x;
    const int first_world_tile_y = scene.camera_tile_y - half_tiles_y;
    const int origin_x = (window_width - view_tiles_x * tile_pixel_size) / 2;
    const int origin_y = (window_height - view_tiles_y * tile_pixel_size) / 2;

    for (const RenderTile& tile : scene.tiles) {
        if (tile.material_id == 0) {
            continue;
        }

        const int local_tile_x = tile.world_tile_x - first_world_tile_x;
        const int local_tile_y = tile.world_tile_y - first_world_tile_y;
        if (local_tile_x < 0 || local_tile_x >= view_tiles_x ||
            local_tile_y < 0 || local_tile_y >= view_tiles_y) {
            continue;
        }

        const int screen_x = origin_x + local_tile_x * tile_pixel_size;
        const int screen_y = origin_y + local_tile_y * tile_pixel_size;
        const RgbaColor tile_color = ApplyTileVariation(
            MaterialColor(tile.material_id),
            tile.world_tile_x,
            tile.world_tile_y);
        DrawFilledRect(
            renderer_,
            screen_x,
            screen_y,
            tile_pixel_size,
            tile_pixel_size,
            tile_color);
    }

    const int player_local_x = scene.player_tile_x - first_world_tile_x;
    const int player_local_y = scene.player_tile_y - first_world_tile_y;
    DrawFilledRect(
        renderer_,
        origin_x + player_local_x * tile_pixel_size + tile_pixel_size / 6,
        origin_y + player_local_y * tile_pixel_size + tile_pixel_size / 8,
        tile_pixel_size * 2 / 3,
        tile_pixel_size * 3 / 4,
        RgbaColor{
            .r = 72,
            .g = 196,
            .b = 248,
            .a = 255,
        });

    const int hud_x = 12;
    const int hud_y = 12;
    const int hud_width = 220;
    const int hud_height = 74;
    DrawFilledRect(
        renderer_,
        hud_x,
        hud_y,
        hud_width,
        hud_height,
        RgbaColor{
            .r = 18,
            .g = 18,
            .b = 22,
            .a = 214,
        });

    const int bar_max_width = 120;
    const int dirt_bar_width =
        std::min<int>(bar_max_width, static_cast<int>(scene.hud.dirt_count) * 4);
    const int stone_bar_width =
        std::min<int>(bar_max_width, static_cast<int>(scene.hud.stone_count) * 4);
    DrawFilledRect(
        renderer_,
        hud_x + 72,
        hud_y + 14,
        dirt_bar_width,
        14,
        MaterialColor(1));
    DrawFilledRect(
        renderer_,
        hud_x + 72,
        hud_y + 38,
        stone_bar_width,
        14,
        MaterialColor(2));

    const int selector_x = hud_x + 18;
    const int selector_y = hud_y + 16;
    DrawFilledRect(
        renderer_,
        selector_x,
        selector_y,
        20,
        20,
        MaterialColor(1));
    DrawFilledRect(
        renderer_,
        selector_x + 28,
        selector_y,
        20,
        20,
        MaterialColor(2));

    const bool dirt_selected = scene.hud.selected_material_id == 1;
    const bool stone_selected = scene.hud.selected_material_id == 2;
    DrawFilledRect(
        renderer_,
        selector_x - 2,
        selector_y - 2,
        24,
        24,
        dirt_selected ? RgbaColor{.r = 240, .g = 214, .b = 108, .a = 255}
                      : RgbaColor{.r = 36, .g = 36, .b = 38, .a = 255});
    DrawFilledRect(
        renderer_,
        selector_x + 26,
        selector_y - 2,
        24,
        24,
        stone_selected ? RgbaColor{.r = 240, .g = 214, .b = 108, .a = 255}
                       : RgbaColor{.r = 36, .g = 36, .b = 38, .a = 255});
    DrawFilledRect(
        renderer_,
        selector_x,
        selector_y,
        20,
        20,
        MaterialColor(1));
    DrawFilledRect(
        renderer_,
        selector_x + 28,
        selector_y,
        20,
        20,
        MaterialColor(2));

    SDL_RenderPresent(renderer_);
}

void SdlContext::Shutdown() {
    if (renderer_ != nullptr) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }

    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }

    if (SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        SDL_Quit();
    }
}

}  // namespace novaria::platform
