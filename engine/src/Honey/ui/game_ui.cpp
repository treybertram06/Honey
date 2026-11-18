#include "hnpch.h"
#include "game_ui.h"

#include "imgui.h"
#include "Honey/core/asset_paths.h"
#include <glm/common.hpp>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Honey {

    namespace {

        constexpr float k_reference_height = 1080.0f; // virtual pixel space scripts author against
        constexpr float k_min_scale = 0.5f;
        constexpr float k_max_scale = 4.0f;

        // Number of theme pushes made in begin_frame; end_frame pops exactly this many.
        constexpr int k_theme_color_count = 1;

        glm::vec2 anchor_to_pivot(GameUI::UIAnchor anchor) {
            // Calculate pivot based on enum integer values (enum is row-major order)
            int column = static_cast<int>(anchor) % 3;
            int row =    static_cast<int>(anchor) / 3;
            return { column * 0.5f, row * 0.5f };
        }

        // True when it is safe to issue ImGui calls (a context exists). Renderer/OpenGL/odd startup
        // states without ImGui turn the whole API into no-ops instead of crashing.
        bool has_imgui() { return ImGui::GetCurrentContext() != nullptr; }

        ImVec4 to_im(const glm::vec4& v) { return {v.r, v.g, v.b, v.a}; }
        ImVec4 with_alpha(const glm::vec4& v, float a) { return {v.r, v.g, v.b, a}; }

        // Font handle -> ImFont*. Slot 0 is the default font (nullptr = keep the current one).
        std::vector<ImFont*> g_fonts{nullptr};
        std::unordered_map<std::string, int> g_font_by_path;

        ImFont* font_from_handle(int handle) {
            return (handle > 0 && handle < int(g_fonts.size())) ? g_fonts[handle] : nullptr;
        }

        // Per-button hover fade (0..1), keyed by ImGui id. Read before drawing, updated after.
        std::unordered_map<ImGuiID, float> g_hover_fade;

    }

    GameUI::UIRect GameUI::s_rect = {};
    GameUI::UITheme GameUI::s_theme = {};
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

        ImGui::PushStyleColor(ImGuiCol_Text, to_im(s_theme.text_color));

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

        // Window-level style is read by Begin(), so it is pushed before and popped right after.
        const float pad = s_theme.panel_padding * s_scale;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { pad, pad });
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, s_theme.rounding * 1.5f * s_scale);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, to_im(s_theme.panel_color));
        ImGui::PushStyleColor(ImGuiCol_Border, with_alpha(s_theme.accent_color, 0.35f)); // faint accent edge
        ImGui::Begin(name.c_str(), nullptr, flags);
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);

        // ItemSpacing applies to widgets submitted inside the window; popped in end_panel before End().
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { s_theme.item_spacing * 0.6f * s_scale, s_theme.item_spacing * s_scale });

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

    void GameUI::label(const char* text, float size, glm::vec4 color, int font, int align) {
        if (!has_imgui() || !s_frame_active) return;

        ImGui::PushFont(font_from_handle(font), size * s_scale);
        ImGui::PushStyleColor(ImGuiCol_Text, {color.r, color.g, color.b, color.a});
        if (align == 1 || align == 2) {
            // Shift the cursor by the free space. On an auto-fit panel the widest item defines the
            // width, so this settles to 0 offset for the widest item and centers the narrower ones.
            const float free_space = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(text).x;
            if (free_space > 0.0f)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + free_space * (align == 1 ? 0.5f : 1.0f));
        }
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    bool GameUI::button(const char* text, float width) {
        if (!has_imgui() || !s_frame_active) return false;

        // Sizes are in virtual pixels (1080-high reference) and scaled to the current rect.
        const UITheme& t = s_theme;
        const glm::vec4& accent = t.accent_color;
        const float rounding = t.rounding * s_scale;

        // Hover fade: colours are driven by last frame's fade value so hover eases in/out instead of
        // snapping. ImGui's own hover/active colour swap is neutralised by pushing the same faded colour.
        const ImGuiID id = ImGui::GetID(text);
        if (g_hover_fade.size() > 512) g_hover_fade.clear(); // bound growth from dynamic labels
        float& fade = g_hover_fade[id];

        const ImVec4 idle  = {1.0f, 1.0f, 1.0f, 0.07f};
        const ImVec4 hover = with_alpha(accent, 0.30f);
        const ImVec4 faded = {idle.x + (hover.x - idle.x) * fade, idle.y + (hover.y - idle.y) * fade,
                              idle.z + (hover.z - idle.z) * fade, idle.w + (hover.w - idle.w) * fade};

        // Every push below is popped before returning; never early-return between them.
        ImGui::PushFont(font_from_handle(t.font), t.button_font_size * s_scale);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { t.button_padding_x * s_scale, t.button_padding_y * s_scale });
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rounding);
        ImGui::PushStyleColor(ImGuiCol_Button, faded);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, faded);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, with_alpha(accent, 0.65f));

        // width 0 => fit the text; height 0 => text height + vertical padding.
        // Button() returns true once, on the frame the mouse is released over it.
        const bool clicked = ImGui::Button(text, { width * s_scale, 0.0f });

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);
        ImGui::PopFont();

        // Update the fade and draw an accent outline that grows with it.
        const bool hovered = ImGui::IsItemHovered();
        const float step = ImGui::GetIO().DeltaTime * 12.0f;
        fade = glm::clamp(fade + (hovered ? step : -step), 0.0f, 1.0f);
        if (fade > 0.01f) {
            ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                ImGui::GetColorU32(with_alpha(accent, fade)), rounding, 0, 1.5f * s_scale);
        }
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

    void GameUI::set_theme(const UITheme& theme) {
        s_theme = theme;
    }

    int GameUI::load_font(const char* path) {
        HN_PROFILE_FUNCTION();
        if (!has_imgui() || !path || !*path) return 0;

        auto cached = g_font_by_path.find(path);
        if (cached != g_font_by_path.end()) return cached->second;

        std::filesystem::path file(path);
        if (file.is_relative()) file = AssetPaths::resolve(file);

        // ImGui asserts on a missing file, so check first and fall back to the default font.
        int handle = 0;
        if (!std::filesystem::exists(file)) {
            HN_CORE_WARN("[GameUI] Font not found: {}", file.string());
        } else if (ImFont* font = ImGui::GetIO().Fonts->AddFontFromFileTTF(file.string().c_str(), 0.0f)) {
            g_fonts.push_back(font);
            handle = int(g_fonts.size()) - 1;
        } else {
            HN_CORE_WARN("[GameUI] Failed to load font: {}", file.string());
        }
        g_font_by_path[path] = handle; // cache failures too, so a bad path warns once
        return handle;
    }

}
