#include "ui/gameplay_ui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <utility>

namespace novaria::ui {
namespace {

platform::RgbaColor WithAlpha(platform::RgbaColor color, std::uint8_t alpha) {
    color.a = alpha;
    return color;
}

void PushFilledRect(
    platform::RenderScene& scene,
    platform::RenderLayer layer,
    int z,
    float x,
    float y,
    float width,
    float height,
    platform::RgbaColor color) {
    scene.overlay_commands.push_back(
        platform::RenderCommand::FilledRect(layer, z, x, y, width, height, color));
}

void PushLine(
    platform::RenderScene& scene,
    platform::RenderLayer layer,
    int z,
    float x1,
    float y1,
    float x2,
    float y2,
    platform::RgbaColor color) {
    scene.overlay_commands.push_back(
        platform::RenderCommand::Line(layer, z, x1, y1, x2, y2, color));
}

void PushRectOutline(
    platform::RenderScene& scene,
    platform::RenderLayer layer,
    int z,
    float x,
    float y,
    float width,
    float height,
    platform::RgbaColor color) {
    PushLine(scene, layer, z, x, y, x + width, y, color);
    PushLine(scene, layer, z, x, y + height, x + width, y + height, color);
    PushLine(scene, layer, z, x, y, x, y + height, color);
    PushLine(scene, layer, z, x + width, y, x + width, y + height, color);
}

void PushText(
    platform::RenderScene& scene,
    platform::RenderLayer layer,
    int z,
    float x,
    float y,
    float scale,
    std::string text,
    platform::RgbaColor color) {
    scene.overlay_commands.push_back(
        platform::RenderCommand::Text(layer, z, x, y, scale, std::move(text), color));
}

std::string ToUpperAscii(std::string in) {
    for (char& ch : in) {
        if (static_cast<unsigned char>(ch) < 128) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
    }
    return in;
}

float Clamp01(float v) {
    return std::clamp(v, 0.0F, 1.0F);
}

struct RecipeUiState final {
    const char* title = "";
    const char* req = "";
    bool locked = false;
    bool craftable = false;
};

RecipeUiState ResolveRecipeState(const GameplayUiModel& model, int recipe_index) {
    // 0: Workbench (wood >= 10)
    // 1: Wood Sword (wood >= 7, needs workbench in range)
    // 2: Torch (wood >= 1, coal >= 1)
    switch (recipe_index) {
        case 0: {
            const bool craftable = model.wood_count >= 10;
            return RecipeUiState{
                .title = "WORKBENCH",
                .req = "WOOD >= 10",
                .locked = false,
                .craftable = craftable,
            };
        }
        case 1: {
            const bool locked = !model.workbench_in_range;
            const bool craftable = !locked && model.wood_count >= 7;
            return RecipeUiState{
                .title = "WOOD SWORD",
                .req = "WOOD >= 7 (NEAR WB)",
                .locked = locked,
                .craftable = craftable,
            };
        }
        case 2: {
            const bool craftable = model.wood_count >= 1 && model.coal_count >= 1;
            return RecipeUiState{
                .title = "TORCH x4",
                .req = "WOOD >= 1 + COAL >= 1",
                .locked = false,
                .craftable = craftable,
            };
        }
        default:
            return RecipeUiState{};
    }
}

void AppendWorldOverlay(
    platform::RenderScene& scene,
    const GameplayUiFrameContext& frame,
    const GameplayUiPalette& palette,
    const GameplayUiModel& model) {
    const float tile_px = static_cast<float>(frame.tile_pixel_size);
    const float target_x = frame.world_origin_x + static_cast<float>(model.target_tile_x) * tile_px;
    const float target_y = frame.world_origin_y + static_cast<float>(model.target_tile_y) * tile_px;

    if (model.target_highlight_visible) {
        const platform::RgbaColor outline =
            model.target_reachable ? WithAlpha(palette.accent, 220) : WithAlpha(palette.danger, 220);
        PushRectOutline(scene, platform::RenderLayer::WorldOverlay, 10, target_x + 1.0F, target_y + 1.0F, tile_px - 2.0F, tile_px - 2.0F, outline);
        PushFilledRect(scene, platform::RenderLayer::WorldOverlay, 9, target_x, target_y, tile_px, tile_px, WithAlpha(outline, 40));
    }

    if (!model.action_primary_progress.active) {
        return;
    }
    if (model.action_primary_progress.required_ticks <= 0) {
        return;
    }

    const float ratio =
        Clamp01(static_cast<float>(model.action_primary_progress.elapsed_ticks) /
                static_cast<float>(model.action_primary_progress.required_ticks));

    const float progress_x =
        frame.world_origin_x + static_cast<float>(model.action_primary_progress.target_tile_x) * tile_px;
    const float progress_y =
        frame.world_origin_y + static_cast<float>(model.action_primary_progress.target_tile_y) * tile_px;

    const float bar_w = tile_px;
    const float bar_h = 6.0F;
    const float bar_x = progress_x;
    const float bar_y = progress_y - 10.0F;
    PushFilledRect(scene, platform::RenderLayer::WorldOverlay, 20, bar_x, bar_y, bar_w, bar_h, WithAlpha(palette.panel, 220));
    PushFilledRect(scene, platform::RenderLayer::WorldOverlay, 21, bar_x, bar_y, bar_w * ratio, bar_h,
        model.action_primary_progress.is_place ? WithAlpha(palette.torch, 230) : WithAlpha(palette.accent, 230));
    PushRectOutline(scene, platform::RenderLayer::WorldOverlay, 22, bar_x, bar_y, bar_w, bar_h, WithAlpha(palette.border, 200));
}

void AppendHud(
    platform::RenderScene& scene,
    const GameplayUiFrameContext& frame,
    const GameplayUiPalette& palette,
    const GameplayUiModel& model) {
    const int vw = std::max(1, frame.viewport_width);
    const int vh = std::max(1, frame.viewport_height);

    // Health (top-left)
    {
        const int x = 16;
        const int y = 14;
        const int w = 220;
        const int h = 28;
        PushFilledRect(scene, platform::RenderLayer::UI, 10, static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), palette.panel);
        PushRectOutline(scene, platform::RenderLayer::UI, 11, static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), palette.border);

        const float ratio = (model.hp_max == 0) ? 0.0F : Clamp01(static_cast<float>(model.hp_current) / static_cast<float>(model.hp_max));
        const int bar_pad = 4;
        const int bar_x = x + bar_pad;
        const int bar_y = y + 14;
        const int bar_w = w - bar_pad * 2;
        PushFilledRect(scene, platform::RenderLayer::UI, 12, static_cast<float>(bar_x), static_cast<float>(bar_y), static_cast<float>(bar_w), 8.0F, WithAlpha(palette.border, 110));
        PushFilledRect(scene, platform::RenderLayer::UI, 13, static_cast<float>(bar_x), static_cast<float>(bar_y), static_cast<float>(bar_w) * ratio, 8.0F,
            (ratio < 0.25F) ? palette.danger : platform::RgbaColor{.r = 204, .g = 62, .b = 74, .a = 255});

        PushText(scene, platform::RenderLayer::UI, 20, static_cast<float>(x + 8), static_cast<float>(y + 6), 2.0F,
            "HP " + std::to_string(model.hp_current) + "/" + std::to_string(model.hp_max),
            palette.text);
    }

    // Resource badges (top-right)
    {
        struct Row final {
            const char* label;
            std::uint32_t value;
            platform::RgbaColor color;
        };
        const Row rows[] = {
            {"DIRT", model.dirt_count, palette.dirt},
            {"STONE", model.stone_count, palette.stone},
            {"WOOD", model.wood_count, palette.wood},
            {"COAL", model.coal_count, palette.coal},
            {"TORCH", model.torch_count, palette.torch},
        };

        const int row_h = 22;
        const int panel_w = 200;
        const int x = vw - panel_w - 16;
        const int y = 14;
        const int h = static_cast<int>(std::size(rows)) * row_h + 12;

        PushFilledRect(scene, platform::RenderLayer::UI, 10, static_cast<float>(x), static_cast<float>(y), static_cast<float>(panel_w), static_cast<float>(h), WithAlpha(palette.panel, 210));
        PushRectOutline(scene, platform::RenderLayer::UI, 11, static_cast<float>(x), static_cast<float>(y), static_cast<float>(panel_w), static_cast<float>(h), palette.border);

        for (int i = 0; i < static_cast<int>(std::size(rows)); ++i) {
            const int ry = y + 8 + i * row_h;
            PushFilledRect(scene, platform::RenderLayer::UI, 12, static_cast<float>(x + 10), static_cast<float>(ry + 4), 10.0F, 10.0F, WithAlpha(rows[i].color, 240));
            PushText(scene, platform::RenderLayer::UI, 20, static_cast<float>(x + 26), static_cast<float>(ry + 2), 2.0F,
                std::string(rows[i].label) + " " + std::to_string(rows[i].value),
                palette.text);
        }
    }

    // Hotbar (bottom-center)
    {
        const int slot_size = 30;
        const int slot_gap = 6;
        const int slot_count = 10;
        const int bar_w = slot_count * slot_size + (slot_count - 1) * slot_gap;
        const int bar_h = slot_size;
        const int x = (vw - bar_w) / 2;
        const int y = vh - bar_h - 18;

        for (int i = 0; i < slot_count; ++i) {
            const int sx = x + i * (slot_size + slot_gap);
            const bool selected = i == static_cast<int>(model.selected_hotbar_slot);
            const bool context = model.context_slot_visible && i == static_cast<int>(model.context_slot_current);

            platform::RgbaColor bg = WithAlpha(palette.panel_2, 220);
            platform::RgbaColor border = WithAlpha(palette.border, 220);
            if (context) {
                border = WithAlpha(palette.accent, 255);
            } else if (selected) {
                border = WithAlpha(palette.torch, 255);
            }

            PushFilledRect(scene, platform::RenderLayer::UI, 10, static_cast<float>(sx), static_cast<float>(y), static_cast<float>(slot_size), static_cast<float>(slot_size), bg);
            PushRectOutline(scene, platform::RenderLayer::UI, 11, static_cast<float>(sx), static_cast<float>(y), static_cast<float>(slot_size), static_cast<float>(slot_size), border);
            PushText(scene, platform::RenderLayer::UI, 12, static_cast<float>(sx + 9), static_cast<float>(y + 8), 2.0F, std::to_string((i + 1) % 10), WithAlpha(palette.text_muted, 230));
        }

        // Row + Smart pills
        const int pill_y = y - 24;
        const int left_pill_w = 120;
        PushFilledRect(scene, platform::RenderLayer::UI, 10, static_cast<float>(x), static_cast<float>(pill_y), static_cast<float>(left_pill_w), 18.0F, WithAlpha(palette.panel, 210));
        PushRectOutline(scene, platform::RenderLayer::UI, 11, static_cast<float>(x), static_cast<float>(pill_y), static_cast<float>(left_pill_w), 18.0F, palette.border);
        PushText(scene, platform::RenderLayer::UI, 12, static_cast<float>(x + 8), static_cast<float>(pill_y + 2), 2.0F,
            "ROW " + std::to_string(static_cast<int>(model.active_hotbar_row) + 1) + "/" + std::to_string(static_cast<int>(model.hotbar_rows)),
            palette.text);

        const int smart_pill_w = 130;
        const int smart_x = x + bar_w - smart_pill_w;
        const platform::RgbaColor smart_bg = model.smart_mode_enabled ? WithAlpha(palette.accent, 210) : WithAlpha(palette.panel, 210);
        PushFilledRect(scene, platform::RenderLayer::UI, 10, static_cast<float>(smart_x), static_cast<float>(pill_y), static_cast<float>(smart_pill_w), 18.0F, smart_bg);
        PushRectOutline(scene, platform::RenderLayer::UI, 11, static_cast<float>(smart_x), static_cast<float>(pill_y), static_cast<float>(smart_pill_w), 18.0F, palette.border);
        PushText(scene, platform::RenderLayer::UI, 12, static_cast<float>(smart_x + 8), static_cast<float>(pill_y + 2), 2.0F,
            std::string("SMART ") + (model.smart_mode_enabled ? "ON" : "OFF"),
            palette.text);
    }

    // Milestones + short status (bottom-left)
    {
        const int x = 16;
        const int y = vh - 72;
        const platform::RgbaColor wb = model.workbench_built ? palette.success : WithAlpha(palette.panel, 210);
        const platform::RgbaColor sw = model.wood_sword_crafted ? WithAlpha(palette.text, 220) : WithAlpha(palette.panel, 210);
        PushFilledRect(scene, platform::RenderLayer::UI, 10, static_cast<float>(x), static_cast<float>(y), 120.0F, 18.0F, wb);
        PushText(scene, platform::RenderLayer::UI, 11, static_cast<float>(x + 8), static_cast<float>(y + 2), 2.0F, "WB", palette.text);
        PushFilledRect(scene, platform::RenderLayer::UI, 10, static_cast<float>(x + 130), static_cast<float>(y), 120.0F, 18.0F, sw);
        PushText(scene, platform::RenderLayer::UI, 11, static_cast<float>(x + 138), static_cast<float>(y + 2), 2.0F, "SWORD", palette.text);
    }

    // Pickup toast
    if (model.pickup_toast_ticks_remaining > 0 && model.pickup_toast_amount > 0) {
        const float t = Clamp01(static_cast<float>(model.pickup_toast_ticks_remaining) / 90.0F);
        const int w = std::min(320, 160 + static_cast<int>(model.pickup_toast_label.size()) * 10);
        const int h = 20;
        const int x = 16;
        const int y = vh - 104;
        PushFilledRect(scene, platform::RenderLayer::UI, 30, static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), WithAlpha(palette.panel, 220));
        PushRectOutline(scene, platform::RenderLayer::UI, 31, static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), WithAlpha(palette.border, 210));
        PushFilledRect(scene, platform::RenderLayer::UI, 32, static_cast<float>(x + 6), static_cast<float>(y + 6), 8.0F, 8.0F, WithAlpha(palette.accent, 220));

        const std::string label = ToUpperAscii(model.pickup_toast_label);
        PushText(scene, platform::RenderLayer::UI, 33, static_cast<float>(x + 20), static_cast<float>(y + 2), 2.0F,
            "+" + std::to_string(model.pickup_toast_amount) + " " + label,
            palette.text);
        (void)t;
    }

    // Interaction toast (short, for feedback)
    if (model.last_interaction_ticks_remaining > 0) {
        const int x = 16;
        const int y = 46;
        std::string msg;
        if (model.last_interaction_type == 1) {
            msg = "WORKBENCH";
        } else if (model.last_interaction_type == 2) {
            msg = "CRAFT";
        } else {
            msg = "INTERACT";
        }
        PushFilledRect(scene, platform::RenderLayer::UI, 25, static_cast<float>(x), static_cast<float>(y), 120.0F, 18.0F, WithAlpha(palette.panel, 210));
        PushRectOutline(scene, platform::RenderLayer::UI, 26, static_cast<float>(x), static_cast<float>(y), 120.0F, 18.0F, palette.border);
        PushText(scene, platform::RenderLayer::UI, 27, static_cast<float>(x + 8), static_cast<float>(y + 2), 2.0F, msg, palette.text_muted);
    }
}

void AppendInventoryOverlay(
    platform::RenderScene& scene,
    const GameplayUiFrameContext& frame,
    const GameplayUiPalette& palette,
    const GameplayUiModel& model) {
    if (!model.inventory_open) {
        return;
    }

    const int vw = std::max(1, frame.viewport_width);
    const int vh = std::max(1, frame.viewport_height);

    PushFilledRect(scene, platform::RenderLayer::UI, 200, 0.0F, 0.0F, static_cast<float>(vw), static_cast<float>(vh), platform::RgbaColor{.r = 0, .g = 0, .b = 0, .a = 140});

    const int panel_w = std::min(920, vw - 80);
    const int panel_h = std::min(520, vh - 100);
    const int px = (vw - panel_w) / 2;
    const int py = (vh - panel_h) / 2;

    PushFilledRect(scene, platform::RenderLayer::UI, 210, static_cast<float>(px), static_cast<float>(py), static_cast<float>(panel_w), static_cast<float>(panel_h), WithAlpha(palette.panel, 235));
    PushRectOutline(scene, platform::RenderLayer::UI, 211, static_cast<float>(px), static_cast<float>(py), static_cast<float>(panel_w), static_cast<float>(panel_h), WithAlpha(palette.border, 230));

    PushText(scene, platform::RenderLayer::UI, 212, static_cast<float>(px + 16), static_cast<float>(py + 12), 2.0F, "INVENTORY", palette.text);
    PushText(scene, platform::RenderLayer::UI, 212, static_cast<float>(px + 16), static_cast<float>(py + 32), 2.0F, "W/S SELECT  |  ENTER/RIGHT-CLICK CRAFT  |  ESC CLOSE", palette.text_muted);

    const int left_w = 320;
    const int content_y = py + 64;
    const int content_h = panel_h - 80;

    // Recipe list
    PushFilledRect(scene, platform::RenderLayer::UI, 220, static_cast<float>(px + 16), static_cast<float>(content_y), static_cast<float>(left_w), static_cast<float>(content_h), WithAlpha(palette.panel_2, 235));
    PushRectOutline(scene, platform::RenderLayer::UI, 221, static_cast<float>(px + 16), static_cast<float>(content_y), static_cast<float>(left_w), static_cast<float>(content_h), WithAlpha(palette.border, 220));

    const int recipe_count = 3;
    for (int i = 0; i < recipe_count; ++i) {
        const int card_h = 70;
        const int cx = px + 28;
        const int cy = content_y + 12 + i * (card_h + 10);
        const RecipeUiState st = ResolveRecipeState(model, i);
        const bool selected = i == static_cast<int>(model.selected_recipe_index);

        platform::RgbaColor card = WithAlpha(palette.panel, 220);
        platform::RgbaColor border = WithAlpha(palette.border, 220);
        platform::RgbaColor title = palette.text;
        platform::RgbaColor subtitle = palette.text_muted;
        if (st.locked) {
            border = WithAlpha(palette.danger, 220);
            subtitle = WithAlpha(palette.danger, 230);
        } else if (st.craftable) {
            border = WithAlpha(palette.success, 230);
        }
        if (selected) {
            card = WithAlpha(palette.panel_2, 245);
            border = WithAlpha(palette.accent, 255);
        }

        PushFilledRect(scene, platform::RenderLayer::UI, 230, static_cast<float>(cx), static_cast<float>(cy), static_cast<float>(left_w - 24), static_cast<float>(card_h), card);
        PushRectOutline(scene, platform::RenderLayer::UI, 231, static_cast<float>(cx), static_cast<float>(cy), static_cast<float>(left_w - 24), static_cast<float>(card_h), border);
        PushText(scene, platform::RenderLayer::UI, 232, static_cast<float>(cx + 10), static_cast<float>(cy + 10), 2.0F, st.title, title);
        PushText(scene, platform::RenderLayer::UI, 232, static_cast<float>(cx + 10), static_cast<float>(cy + 34), 2.0F, st.req, subtitle);
    }

    // Inventory summary
    const int right_x = px + 16 + left_w + 16;
    const int right_w = panel_w - (right_x - px) - 16;
    PushFilledRect(scene, platform::RenderLayer::UI, 220, static_cast<float>(right_x), static_cast<float>(content_y), static_cast<float>(right_w), static_cast<float>(content_h), WithAlpha(palette.panel_2, 235));
    PushRectOutline(scene, platform::RenderLayer::UI, 221, static_cast<float>(right_x), static_cast<float>(content_y), static_cast<float>(right_w), static_cast<float>(content_h), WithAlpha(palette.border, 220));

    int ry = content_y + 16;
    auto row = [&](const char* label, std::uint32_t value, platform::RgbaColor color) {
        PushFilledRect(scene, platform::RenderLayer::UI, 222, static_cast<float>(right_x + 16), static_cast<float>(ry + 6), 10.0F, 10.0F, WithAlpha(color, 240));
        PushText(scene, platform::RenderLayer::UI, 223, static_cast<float>(right_x + 32), static_cast<float>(ry + 4), 2.0F, std::string(label) + " " + std::to_string(value), palette.text);
        ry += 24;
    };

    PushText(scene, platform::RenderLayer::UI, 222, static_cast<float>(right_x + 16), static_cast<float>(content_y + 10), 2.0F, "BACKPACK (PROTOTYPE)", palette.text_muted);
    ry = content_y + 34;
    row("DIRT", model.dirt_count, palette.dirt);
    row("STONE", model.stone_count, palette.stone);
    row("WOOD", model.wood_count, palette.wood);
    row("COAL", model.coal_count, palette.coal);
    row("TORCH", model.torch_count, palette.torch);
    row("WORKBENCH", model.workbench_count, palette.workbench);
    row("SWORD", model.wood_sword_count, WithAlpha(palette.text, 220));

    if (!model.workbench_in_range) {
        PushText(scene, platform::RenderLayer::UI, 240, static_cast<float>(right_x + 16), static_cast<float>(py + panel_h - 34), 2.0F,
            "WORKBENCH RECIPES REQUIRE RANGE <= 4",
            WithAlpha(palette.danger, 235));
    } else {
        PushText(scene, platform::RenderLayer::UI, 240, static_cast<float>(right_x + 16), static_cast<float>(py + panel_h - 34), 2.0F,
            "WORKBENCH IN RANGE",
            WithAlpha(palette.success, 235));
    }

    // "Paused" stamp (soft pause)
    PushText(scene, platform::RenderLayer::UI, 250, static_cast<float>(px + panel_w - 140), static_cast<float>(py + 12), 2.0F, "PAUSED", WithAlpha(palette.text_muted, 230));
}

}  // namespace

void AppendGameplayUi(
    platform::RenderScene& scene,
    const GameplayUiFrameContext& frame,
    const GameplayUiPalette& palette,
    const GameplayUiModel& model) {
    AppendWorldOverlay(scene, frame, palette, model);
    AppendHud(scene, frame, palette, model);
    AppendInventoryOverlay(scene, frame, palette, model);
}

}  // namespace novaria::ui
