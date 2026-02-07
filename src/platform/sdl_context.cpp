#include "platform/sdl_context.h"

#include "core/logger.h"
#include "world/world_service_basic.h"

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

Uint8 LerpByte(Uint8 from, Uint8 to, float factor) {
    const float clamped = std::clamp(factor, 0.0F, 1.0F);
    return ClampToByte(
        static_cast<int>(static_cast<float>(from) * (1.0F - clamped) +
                         static_cast<float>(to) * clamped));
}

RgbaColor MaterialColor(std::uint16_t material_id) {
    switch (material_id) {
        case world::WorldServiceBasic::kMaterialDirt:
            return RgbaColor{
                .r = 126,
                .g = 88,
                .b = 50,
                .a = 255,
            };
        case world::WorldServiceBasic::kMaterialStone:
            return RgbaColor{
                .r = 116,
                .g = 122,
                .b = 132,
                .a = 255,
            };
        case world::WorldServiceBasic::kMaterialGrass:
            return RgbaColor{
                .r = 82,
                .g = 160,
                .b = 58,
                .a = 255,
            };
        case world::WorldServiceBasic::kMaterialWater:
            return RgbaColor{
                .r = 58,
                .g = 124,
                .b = 206,
                .a = 190,
            };
        case world::WorldServiceBasic::kMaterialWood:
            return RgbaColor{
                .r = 124,
                .g = 84,
                .b = 44,
                .a = 255,
            };
        case world::WorldServiceBasic::kMaterialLeaves:
            return RgbaColor{
                .r = 64,
                .g = 128,
                .b = 52,
                .a = 235,
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

RgbaColor ApplyLightToColor(const RgbaColor& base_color, std::uint8_t light_level) {
    return RgbaColor{
        .r = static_cast<Uint8>((static_cast<unsigned int>(base_color.r) * light_level) / 255U),
        .g = static_cast<Uint8>((static_cast<unsigned int>(base_color.g) * light_level) / 255U),
        .b = static_cast<Uint8>((static_cast<unsigned int>(base_color.b) * light_level) / 255U),
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
    (void)SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

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
            if (event.key.scancode == SDL_SCANCODE_1) {
                out_actions.hotbar_select_slot_1 = true;
            }

            if (event.key.scancode == SDL_SCANCODE_2) {
                out_actions.hotbar_select_slot_2 = true;
            }

            if (event.key.scancode == SDL_SCANCODE_3) {
                out_actions.hotbar_select_slot_3 = true;
            }

            if (event.key.scancode == SDL_SCANCODE_4) {
                out_actions.hotbar_select_slot_4 = true;
            }

            if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                out_actions.ui_inventory_toggle_pressed = true;
            }

            if (event.key.scancode == SDL_SCANCODE_TAB) {
                out_actions.hotbar_select_next_row = true;
            }

            if (event.key.scancode == SDL_SCANCODE_LCTRL ||
                event.key.scancode == SDL_SCANCODE_RCTRL) {
                out_actions.smart_mode_toggle_pressed = true;
            }
        }

        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (event.button.button == SDL_BUTTON_RIGHT) {
                out_actions.interaction_primary_pressed = true;
            }
        }
    }

    SDL_PumpEvents();
    float mouse_x = 0.0F;
    float mouse_y = 0.0F;
    const SDL_MouseButtonFlags mouse_buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
    (void)mouse_x;
    (void)mouse_y;
    out_actions.action_primary_held = (mouse_buttons & SDL_BUTTON_LMASK) != 0U;

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
        out_actions.smart_context_held = keyboard_state[SDL_SCANCODE_LSHIFT] ||
            keyboard_state[SDL_SCANCODE_RSHIFT];
    }

    return true;
}

void SdlContext::RenderFrame(float interpolation_alpha, const RenderScene& scene) {
    if (renderer_ == nullptr) {
        return;
    }
    (void)interpolation_alpha;
    const float daylight = std::clamp(scene.daylight_factor, 0.0F, 1.0F);
    const Uint8 sky_r = LerpByte(12, 112, daylight);
    const Uint8 sky_g = LerpByte(18, 170, daylight);
    const Uint8 sky_b = LerpByte(34, 236, daylight);
    (void)SDL_SetRenderDrawColor(renderer_, sky_r, sky_g, sky_b, 255);
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
        if (tile.material_id == world::WorldServiceBasic::kMaterialAir) {
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
        const RgbaColor varied_tile_color = ApplyTileVariation(
            MaterialColor(tile.material_id),
            tile.world_tile_x,
            tile.world_tile_y);
        const RgbaColor tile_color = ApplyLightToColor(varied_tile_color, tile.light_level);
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
    const int hud_width = 280;
    const int hud_height = 116;
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

    const int bar_max_width = 136;
    const int dirt_bar_width =
        std::min<int>(bar_max_width, static_cast<int>(scene.hud.dirt_count) * 4);
    const int stone_bar_width =
        std::min<int>(bar_max_width, static_cast<int>(scene.hud.stone_count) * 4);
    const int wood_bar_width =
        std::min<int>(bar_max_width, static_cast<int>(scene.hud.wood_count) * 4);
    DrawFilledRect(
        renderer_,
        hud_x + 96,
        hud_y + 14,
        dirt_bar_width,
        12,
        MaterialColor(world::WorldServiceBasic::kMaterialDirt));
    DrawFilledRect(
        renderer_,
        hud_x + 96,
        hud_y + 34,
        stone_bar_width,
        12,
        MaterialColor(world::WorldServiceBasic::kMaterialStone));
    DrawFilledRect(
        renderer_,
        hud_x + 96,
        hud_y + 54,
        wood_bar_width,
        12,
        MaterialColor(world::WorldServiceBasic::kMaterialWood));

    const int selector_x = hud_x + 18;
    const int selector_y = hud_y + 16;
    const bool dirt_selected =
        scene.hud.selected_material_id == world::WorldServiceBasic::kMaterialDirt;
    const bool stone_selected =
        scene.hud.selected_material_id == world::WorldServiceBasic::kMaterialStone;
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
        MaterialColor(world::WorldServiceBasic::kMaterialDirt));
    DrawFilledRect(
        renderer_,
        selector_x + 28,
        selector_y,
        20,
        20,
        MaterialColor(world::WorldServiceBasic::kMaterialStone));

    const int status_x = hud_x + 16;
    const int status_y = hud_y + 86;
    DrawFilledRect(
        renderer_,
        status_x,
        status_y,
        116,
        12,
        scene.hud.workbench_built ? RgbaColor{.r = 104, .g = 188, .b = 98, .a = 255}
                                  : RgbaColor{.r = 70, .g = 42, .b = 28, .a = 255});
    DrawFilledRect(
        renderer_,
        status_x + 128,
        status_y,
        116,
        12,
        scene.hud.wood_sword_crafted ? RgbaColor{.r = 186, .g = 210, .b = 228, .a = 255}
                                     : RgbaColor{.r = 54, .g = 58, .b = 64, .a = 255});

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
