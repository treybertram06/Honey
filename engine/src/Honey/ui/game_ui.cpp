#include "hnpch.h"
#include "game_ui.h"

#include "imgui.h"
#include <glm/common.hpp>

namespace Honey {

    namespace {

        constexpr float k_reference_height = 1080.0f; // virtual pixel space scripts author against
        constexpr float k_min_scale = 0.5f;
        constexpr float k_max_scale = 4.0f;

        // Number of theme pushes made in begin_frame; end_frame pops exactly this many.
        constexpr int k_theme_color_count = 4;

        glm::vec2 anchor_to_pivot(GameUI::UIAnchor anchor) {
            // Calculate pivot based on enum integer values (enum is row-major order)
            int column = static_cast<int>(anchor) % 3;
            int row =    static_cast<int>(anchor) / 3;
            return { column * 0.5f, row * 0.5f };
        }

        // True when it is safe to issue ImGui calls (a context exists). Renderer/OpenGL/odd startup
        // states without ImGui turn the whole API into no-ops instead of crashing.
        bool has_imgui() { return ImGui::GetCurrentContext() != nullptr; }

    }

    UIRect GameUI::s_rect = {};
    float GameUI::s_scale = 1.0f;
    uint32_t GameUI::s_panel_depth = 0;
    bool GameUI::s_frame_active = false;

    void GameUI::begin_frame(glm::vec2 origin, glm::vec2 size) {
        HN_PROFILE_FUNCTION();
        if (!has_imgui()) return;

        // A previous frame that was never ended would leave the theme pushed twice.
        if (s_frame_active) end_frame();

        s_rect.origin = origin;
        s_rect.size = size;
        s_scale = glm::clamp(size.y / k_reference_height, k_min_scale, k_max_scale);
        s_panel_depth = 0;

        // TODO (Step 8): replace with a real theme struct. Panel/window background is not part of
        // the theme yet: panels are transparent unless begin_panel asks for a background.
        ImGui::PushStyleColor(ImGuiCol_Text,          { 1.00f, 1.00f, 1.00f, 1.00f });
        ImGui::PushStyleColor(ImGuiCol_Button,        { 0.18f, 0.18f, 0.20f, 0.85f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.30f, 0.30f, 0.34f, 0.95f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  { 0.45f, 0.35f, 0.10f, 1.00f });

        s_frame_active = true;
    }

    void GameUI::end_frame() {
        HN_PROFILE_FUNCTION();
        if (!has_imgui() || !s_frame_active) return;

        // Safety net: a script that threw or forgot end_panel must not leave ImGui unbalanced.
        if (s_panel_depth > 0) {
            HN_CORE_WARN("[GameUI] {} panel(s) left open at end of frame; closing them", s_panel_depth);
            while (s_panel_depth > 0)
                end_panel();
        }

        ImGui::PopStyleColor(k_theme_color_count);
        s_frame_active = false;
    }

    static_assert(int(GameUI::UIAnchor::bottom_right) == 8); // Extra check to ensure enum order
    void GameUI::begin_panel(const char* id, UIAnchor anchor, glm::vec2 offset, glm::vec2 size, bool background) {
        if (!has_imgui() || !s_frame_active) return;

        const glm::vec2 pivot = anchor_to_pivot(anchor);

        // Anchor point on the rect, plus the scaled offset. Clamped to the rect so a bad offset can't
        // push the panel's anchor outside the window (with ImGui viewports enabled, a window outside the
        // main viewport can become its own OS window). The panel body can still overflow if it is larger
        // than the space left; that is accepted for now.
        glm::vec2 position = s_rect.origin + pivot * s_rect.size + offset * s_scale;
        position = glm::clamp(position, s_rect.origin, s_rect.origin + s_rect.size);

        ImGui::SetNextWindowPos({position.x, position.y}, ImGuiCond_Always, {pivot.x, pivot.y});
        // A 0 on an axis auto-fits that axis, so (360, 0) means fixed width, content height.
        ImGui::SetNextWindowSize({size.x * s_scale, size.y * s_scale});

        const std::string name = std::string("##hn_ui_") + id;
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking
            | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
        if (!background) flags |= ImGuiWindowFlags_NoBackground;

        // WindowPadding is read by Begin(), so it is pushed before and popped right after.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 16.0f * s_scale, 16.0f * s_scale });
        ImGui::Begin(name.c_str(), nullptr, flags);
        ImGui::PopStyleVar();

        // ItemSpacing applies to widgets submitted inside the window; popped in end_panel before End().
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 8.0f * s_scale, 12.0f * s_scale });

        s_panel_depth++;
    }

    void GameUI::end_panel() {
        if (!has_imgui() || !s_frame_active) return; // matches begin_panel's early-out; nothing was begun

        if (s_panel_depth == 0) {
            HN_CORE_WARN("[GameUI] Called end_panel when no panels are available! How did you get here?");
            return;
        }

        ImGui::PopStyleVar(); // ItemSpacing from begin_panel
        ImGui::End();

        s_panel_depth--;
    }

    void GameUI::label(const char* text, float size, glm::vec4 color) {
        if (!has_imgui() || !s_frame_active) return;

        ImGui::PushFont(nullptr, size * s_scale); // TODO: Replace font with a user specified one
        ImGui::PushStyleColor(ImGuiCol_Text, {color.r, color.g, color.b, color.a});
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    bool GameUI::button(const char* text, float width) {
        if (!has_imgui() || !s_frame_active) return false;

        // Sizes are in virtual pixels (1080-high reference) and scaled to the current rect.
        // TODO: move these into the theme struct (Step 8)
        constexpr float k_font_size = 28.0f;
        constexpr float k_padding_x = 24.0f; // plain floats: glm::vec2 is not constexpr with GLM_FORCE_INTRINSICS
        constexpr float k_padding_y = 14.0f;
        constexpr float k_rounding = 8.0f;

        // Every push below is popped before returning; never early-return between them.
        ImGui::PushFont(nullptr, k_font_size * s_scale);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { k_padding_x * s_scale, k_padding_y * s_scale });
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, k_rounding * s_scale);

        // width 0 => fit the text; height 0 => text height + vertical padding.
        // Button() returns true once, on the frame the mouse is released over it.
        const bool clicked = ImGui::Button(text, { width * s_scale, 0.0f });

        ImGui::PopStyleVar(2);
        ImGui::PopFont();
        return clicked;
    }

    void GameUI::spacing(float h) {
        if (!has_imgui() || !s_frame_active) return;

        ImGui::Dummy({0, h * s_scale});
    }

    bool GameUI::wants_mouse() {
        if (!has_imgui()) return false;

        return ImGui::GetIO().WantCaptureMouse; // Wrong, as this will always return true in the editor. Will fix in Step 7
    }

}