#pragma once
#include <imgui.h>
#include "Honey/renderer/renderer_api.h"

namespace Honey::UI {

    inline void Image(ImTextureID tex, const ImVec2& size,
                      ImVec2 uv0 = ImVec2(0.0f, 0.0f),
                      ImVec2 uv1 = ImVec2(1.0f, 1.0f))
    {
        if (RendererAPI::get_api() == RendererAPI::API::opengl) {
            // ImGui expects (0,0) top-left; your GL textures are loaded flipped,
            // so invert Y when talking to ImGui.
            uv0.y = 1.0f - uv0.y;
            uv1.y = 1.0f - uv1.y;
        }

        ImGui::Image(tex, size, uv0, uv1);
    }

    inline bool ImageButton(const char* id, ImTextureID tex, const ImVec2& size,
                            ImVec2 uv0 = ImVec2(0.0f, 0.0f),
                            ImVec2 uv1 = ImVec2(1.0f, 1.0f),
                            const ImVec2& padding = ImVec2(0,0),
                            const ImVec4& bg_col = ImVec4(0,0,0,0),
                            const ImVec4& tint_col = ImVec4(1,1,1,1))
    {
        if (RendererAPI::get_api() == RendererAPI::API::opengl) {
            uv0.y = 1.0f - uv0.y;
            uv1.y = 1.0f - uv1.y;
        }

        return ImGui::ImageButton(id, tex, size, uv0, uv1, bg_col, tint_col);
    }

} // namespace Honey::UI