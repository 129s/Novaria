#include "platform/sdl_context.h"

#include "core/logger.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <vector>
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

bool IsDebugScancode(SDL_Scancode scancode) {
    return scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F12;
}

Uint8 ClampToByte(int value) {
    return static_cast<Uint8>(std::clamp(value, 0, 255));
}

Uint8 LerpByte(Uint8 from, Uint8 to, float factor) {
    const float clamped = std::clamp(factor, 0.0F, 1.0F);
    return ClampToByte(
        static_cast<int>(static_cast<float>(from) * (1.0F - clamped) +
                         static_cast<float>(to) * clamped));
}

void DrawFilledRect(
    SDL_Renderer* renderer,
    float x,
    float y,
    float width,
    float height,
    const RgbaColor& color) {
    if (renderer == nullptr || width <= 0.0F || height <= 0.0F) {
        return;
    }

    (void)SDL_SetRenderDrawColor(
        renderer,
        static_cast<Uint8>(color.r),
        static_cast<Uint8>(color.g),
        static_cast<Uint8>(color.b),
        static_cast<Uint8>(color.a));
    const SDL_FRect rect{
        x,
        y,
        width,
        height,
    };
    (void)SDL_RenderFillRect(renderer, &rect);
}

void DrawFilledRect(
    SDL_Renderer* renderer,
    int x,
    int y,
    int width,
    int height,
    const RgbaColor& color) {
    DrawFilledRect(
        renderer,
        static_cast<float>(x),
        static_cast<float>(y),
        static_cast<float>(width),
        static_cast<float>(height),
        color);
}

void DrawLine(
    SDL_Renderer* renderer,
    float x1,
    float y1,
    float x2,
    float y2,
    const RgbaColor& color) {
    if (renderer == nullptr) {
        return;
    }

    (void)SDL_SetRenderDrawColor(
        renderer,
        static_cast<Uint8>(color.r),
        static_cast<Uint8>(color.g),
        static_cast<Uint8>(color.b),
        static_cast<Uint8>(color.a));
    (void)SDL_RenderLine(renderer, x1, y1, x2, y2);
}

int LayerSortKey(RenderLayer layer) {
    return static_cast<int>(layer);
}

constexpr int kFontGlyphWidth = 5;
constexpr int kFontGlyphHeight = 7;

struct FontGlyph final {
    std::uint8_t rows[kFontGlyphHeight]{};  // low 5 bits are pixels
};

FontGlyph GlyphFor(char ch) {
    const unsigned char uch = static_cast<unsigned char>(ch);
    char normalized = static_cast<char>(std::toupper(uch));

    switch (normalized) {
    case '0': return FontGlyph{{0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110}};
    case '1': return FontGlyph{{0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}};
    case '2': return FontGlyph{{0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111}};
    case '3': return FontGlyph{{0b11110, 0b00001, 0b00001, 0b01110, 0b00001, 0b00001, 0b11110}};
    case '4': return FontGlyph{{0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010}};
    case '5': return FontGlyph{{0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110}};
    case '6': return FontGlyph{{0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110}};
    case '7': return FontGlyph{{0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000}};
    case '8': return FontGlyph{{0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110}};
    case '9': return FontGlyph{{0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100}};
    case 'A': return FontGlyph{{0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}};
    case 'B': return FontGlyph{{0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110}};
    case 'C': return FontGlyph{{0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110}};
    case 'D': return FontGlyph{{0b11100, 0b10010, 0b10001, 0b10001, 0b10001, 0b10010, 0b11100}};
    case 'E': return FontGlyph{{0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111}};
    case 'F': return FontGlyph{{0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000}};
    case 'G': return FontGlyph{{0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01111}};
    case 'H': return FontGlyph{{0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}};
    case 'I': return FontGlyph{{0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}};
    case 'J': return FontGlyph{{0b00111, 0b00010, 0b00010, 0b00010, 0b00010, 0b10010, 0b01100}};
    case 'K': return FontGlyph{{0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001}};
    case 'L': return FontGlyph{{0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111}};
    case 'M': return FontGlyph{{0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001}};
    case 'N': return FontGlyph{{0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001}};
    case 'O': return FontGlyph{{0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}};
    case 'P': return FontGlyph{{0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000}};
    case 'Q': return FontGlyph{{0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101}};
    case 'R': return FontGlyph{{0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001}};
    case 'S': return FontGlyph{{0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110}};
    case 'T': return FontGlyph{{0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100}};
    case 'U': return FontGlyph{{0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}};
    case 'V': return FontGlyph{{0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100}};
    case 'W': return FontGlyph{{0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b10101, 0b01010}};
    case 'X': return FontGlyph{{0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001}};
    case 'Y': return FontGlyph{{0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100}};
    case 'Z': return FontGlyph{{0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111}};
    case ' ': return FontGlyph{{0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000}};
    case ':': return FontGlyph{{0b00000, 0b00100, 0b00100, 0b00000, 0b00100, 0b00100, 0b00000}};
    case '.': return FontGlyph{{0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00100, 0b00100}};
    case ',': return FontGlyph{{0b00000, 0b00000, 0b00000, 0b00000, 0b00100, 0b00100, 0b01000}};
    case '-': return FontGlyph{{0b00000, 0b00000, 0b00000, 0b01110, 0b00000, 0b00000, 0b00000}};
    case '+': return FontGlyph{{0b00000, 0b00100, 0b00100, 0b11111, 0b00100, 0b00100, 0b00000}};
    case '/': return FontGlyph{{0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b00000, 0b00000}};
    default:
        return FontGlyph{{0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b00000, 0b01000}};  // '?'
    }
}

void DrawText(
    SDL_Renderer* renderer,
    float x,
    float y,
    float scale,
    const std::string& text,
    const RgbaColor& color) {
    if (renderer == nullptr || text.empty()) {
        return;
    }

    const float pixel = std::max(1.0F, scale);
    const float glyph_advance = (static_cast<float>(kFontGlyphWidth) + 1.0F) * pixel;

    float cursor_x = x;
    float cursor_y = y;
    for (char ch : text) {
        if (ch == '\n') {
            cursor_x = x;
            cursor_y += (static_cast<float>(kFontGlyphHeight) + 2.0F) * pixel;
            continue;
        }

        const FontGlyph glyph = GlyphFor(ch);
        for (int row = 0; row < kFontGlyphHeight; ++row) {
            const std::uint8_t bits = glyph.rows[row];
            for (int col = 0; col < kFontGlyphWidth; ++col) {
                const bool on = (bits & (1U << (kFontGlyphWidth - 1 - col))) != 0U;
                if (!on) {
                    continue;
                }
                DrawFilledRect(
                    renderer,
                    cursor_x + static_cast<float>(col) * pixel,
                    cursor_y + static_cast<float>(row) * pixel,
                    pixel,
                    pixel,
                    color);
            }
        }

        cursor_x += glyph_advance;
    }
}

}  // namespace

SdlContext::~SdlContext() {
    Shutdown();
}

bool SdlContext::Initialize(const core::GameConfig& config) {
    debug_input_enabled_ = config.debug_input_enabled;

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
    core::Logger::Info(
        "input",
        "Debug input switch: " + std::string(debug_input_enabled_ ? "enabled" : "disabled"));
    return true;
}

bool SdlContext::PumpEvents(bool& quit_requested, InputActions& out_actions) {
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        if (IsQuitEvent(event.type)) {
            quit_requested = true;
        }

        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            if (IsDebugScancode(event.key.scancode)) {
                if (debug_input_enabled_) {
                    core::Logger::Info(
                        "input",
                        "Debug key pressed (no-op): " +
                            std::string(SDL_GetScancodeName(event.key.scancode)));
                }
                continue;
            }

            if (event.key.scancode == SDL_SCANCODE_SPACE) {
                out_actions.jump_pressed = true;
            }

            if (event.key.scancode == SDL_SCANCODE_W ||
                event.key.scancode == SDL_SCANCODE_UP) {
                out_actions.ui_nav_up_pressed = true;
            }

            if (event.key.scancode == SDL_SCANCODE_S ||
                event.key.scancode == SDL_SCANCODE_DOWN) {
                out_actions.ui_nav_down_pressed = true;
            }

            if (event.key.scancode == SDL_SCANCODE_RETURN ||
                event.key.scancode == SDL_SCANCODE_KP_ENTER) {
                out_actions.ui_nav_confirm_pressed = true;
            }

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

            if (event.key.scancode == SDL_SCANCODE_5) {
                out_actions.hotbar_select_slot_5 = true;
            }

            if (event.key.scancode == SDL_SCANCODE_6) {
                out_actions.hotbar_select_slot_6 = true;
            }

            if (event.key.scancode == SDL_SCANCODE_7) {
                out_actions.hotbar_select_slot_7 = true;
            }

            if (event.key.scancode == SDL_SCANCODE_8) {
                out_actions.hotbar_select_slot_8 = true;
            }

            if (event.key.scancode == SDL_SCANCODE_9) {
                out_actions.hotbar_select_slot_9 = true;
            }

            if (event.key.scancode == SDL_SCANCODE_0) {
                out_actions.hotbar_select_slot_10 = true;
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

        if (event.type == SDL_EVENT_MOUSE_WHEEL) {
            if (event.wheel.y > 0.0F) {
                out_actions.hotbar_cycle_prev = true;
            } else if (event.wheel.y < 0.0F) {
                out_actions.hotbar_cycle_next = true;
            }
        }
    }

    SDL_PumpEvents();
    float mouse_x = 0.0F;
    float mouse_y = 0.0F;
    const SDL_MouseButtonFlags mouse_buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
    out_actions.cursor_valid = true;
    out_actions.cursor_screen_x = static_cast<int>(mouse_x);
    out_actions.cursor_screen_y = static_cast<int>(mouse_y);
    out_actions.action_primary_held = (mouse_buttons & SDL_BUTTON_LMASK) != 0U;

    if (window_ != nullptr) {
        int window_width = 0;
        int window_height = 0;
        (void)SDL_GetWindowSize(window_, &window_width, &window_height);
        out_actions.viewport_width = window_width;
        out_actions.viewport_height = window_height;
    }

    const bool* keyboard_state = SDL_GetKeyboardState(nullptr);
    if (keyboard_state != nullptr) {
        out_actions.move_left = keyboard_state[SDL_SCANCODE_A];
        out_actions.move_right = keyboard_state[SDL_SCANCODE_D];
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
    const float camera_tile_x = scene.camera_tile_x;
    const float camera_tile_y = scene.camera_tile_y;
    const int camera_tile_floor_x = static_cast<int>(std::floor(camera_tile_x));
    const int camera_tile_floor_y = static_cast<int>(std::floor(camera_tile_y));
    const int first_world_tile_x = camera_tile_floor_x - half_tiles_x;
    const int first_world_tile_y = camera_tile_floor_y - half_tiles_y;
    const float world_origin_x =
        static_cast<float>(window_width) * 0.5F -
        camera_tile_x * static_cast<float>(tile_pixel_size);
    const float world_origin_y =
        static_cast<float>(window_height) * 0.5F -
        camera_tile_y * static_cast<float>(tile_pixel_size);
    const float tile_origin_x =
        world_origin_x +
        static_cast<float>(first_world_tile_x * tile_pixel_size);
    const float tile_origin_y =
        world_origin_y +
        static_cast<float>(first_world_tile_y * tile_pixel_size);

    for (const RenderTile& tile : scene.tiles) {
        if (tile.color.a == 0) {
            continue;
        }

        const int local_tile_x = tile.world_tile_x - first_world_tile_x;
        const int local_tile_y = tile.world_tile_y - first_world_tile_y;
        if (local_tile_x < 0 || local_tile_x >= view_tiles_x ||
            local_tile_y < 0 || local_tile_y >= view_tiles_y) {
            continue;
        }

        const float screen_x =
            tile_origin_x + static_cast<float>(local_tile_x * tile_pixel_size);
        const float screen_y =
            tile_origin_y + static_cast<float>(local_tile_y * tile_pixel_size);
        DrawFilledRect(
            renderer_,
            screen_x,
            screen_y,
            static_cast<float>(tile_pixel_size),
            static_cast<float>(tile_pixel_size),
            tile.color);
    }

    std::vector<const RenderCommand*> sorted_commands;
    sorted_commands.reserve(scene.overlay_commands.size());
    for (const RenderCommand& command : scene.overlay_commands) {
        sorted_commands.push_back(&command);
    }
    std::stable_sort(
        sorted_commands.begin(),
        sorted_commands.end(),
        [](const RenderCommand* left, const RenderCommand* right) {
            if (left == nullptr || right == nullptr) {
                return left != nullptr;
            }
            if (left->layer != right->layer) {
                return LayerSortKey(left->layer) < LayerSortKey(right->layer);
            }
            return left->z < right->z;
        });

    for (const RenderCommand* command : sorted_commands) {
        if (command == nullptr || command->color.a == 0) {
            continue;
        }

        switch (command->kind) {
        case RenderCommandKind::FilledRect:
            DrawFilledRect(
                renderer_,
                command->filled_rect.x,
                command->filled_rect.y,
                command->filled_rect.width,
                command->filled_rect.height,
                command->color);
            break;
        case RenderCommandKind::Line:
            DrawLine(
                renderer_,
                command->line.x1,
                command->line.y1,
                command->line.x2,
                command->line.y2,
                command->color);
            break;
        case RenderCommandKind::Text:
            DrawText(
                renderer_,
                command->text.x,
                command->text.y,
                command->text.scale,
                command->text.text,
                command->color);
            break;
        }
    }

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
