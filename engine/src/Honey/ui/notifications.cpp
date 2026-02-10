#include "notifications.h"
#include "hnpch.h"

#include <algorithm>
#include <cmath>

#include <imgui.h>

#include "Honey/core/engine.h"

namespace Honey::UI {
    namespace {

        ImVec4 toast_accent_color(ToastType type) {
            switch (type) {
            case ToastType::Info:    return ImVec4(0.20f, 0.60f, 1.00f, 1.00f);
            case ToastType::Success: return ImVec4(0.20f, 0.85f, 0.35f, 1.00f);
            case ToastType::Warning: return ImVec4(1.00f, 0.75f, 0.20f, 1.00f);
            case ToastType::Error:   return ImVec4(1.00f, 0.25f, 0.25f, 1.00f);
            }
            return ImVec4(0.20f, 0.60f, 1.00f, 1.00f);
        }

        const char* toast_type_label(ToastType type) {
            switch (type) {
            case ToastType::Info:    return "Info";
            case ToastType::Success: return "Success";
            case ToastType::Warning: return "Warning";
            case ToastType::Error:   return "Error";
            }
            return "Info";
        }

        float clamp01(float v) {
            return std::max(0.0f, std::min(1.0f, v));
        }

        float estimate_toast_height(bool has_title, bool has_message) {
            float h = 0.0f;
            h += ImGui::GetTextLineHeightWithSpacing(); // title/type
            if (has_message)
                h += ImGui::GetTextLineHeightWithSpacing(); // message (rough, but stable)
            h += 16.0f; // padding-ish
            return h;
        }

        std::string ellipsize_to_width(const std::string& s, float max_width_px) {
            if (s.empty())
                return {};

            const ImVec2 full = ImGui::CalcTextSize(s.c_str());
            if (full.x <= max_width_px)
                return s;

            const char* ell = "...";
            const float ell_w = ImGui::CalcTextSize(ell).x;

            // Binary search the largest prefix that fits: prefix + "..."
            int lo = 0;
            int hi = (int)s.size();
            int best = 0;

            while (lo <= hi) {
                int mid = (lo + hi) / 2;
                std::string tmp = s.substr(0, mid);
                float w = ImGui::CalcTextSize(tmp.c_str()).x + ell_w;

                if (w <= max_width_px) {
                    best = mid;
                    lo = mid + 1;
                } else {
                    hi = mid - 1;
                }
            }

            if (best <= 0)
                return ell;

            return s.substr(0, best) + ell;
        }

        float button_width_for_label(const std::string& label, float min_w, float max_w) {
            const ImGuiStyle& st = ImGui::GetStyle();
            const float text_w = ImGui::CalcTextSize(label.c_str()).x;
            const float w = text_w + st.FramePadding.x * 2.0f;
            return std::max(min_w, std::min(max_w, w));
        }

    } // namespace

    void NotificationCenter::push_toast(ToastType type, std::string title, std::string message, float seconds) {
        Toast t;
        t.type = type;
        t.title = std::move(title);
        t.message = std::move(message);
        t.time_remaining = std::max(0.1f, seconds);

        // Keep newest last; render bottom->top so the newest appears closest to the corner.
        m_toasts.emplace_back(std::move(t));
    }

    void NotificationCenter::open_confirm(std::string popup_id, std::string title, std::string message, bool danger) {
        open_confirm(std::move(popup_id), std::move(title), std::move(message), danger, {}, {});
    }

    void NotificationCenter::open_confirm(std::string popup_id,
                                         std::string title,
                                         std::string message,
                                         bool danger,
                                         std::function<void()> on_accept,
                                         std::function<void()> on_reject,
                                         std::string accept_button_text,
                                         std::string reject_button_text) {
        if (popup_id.empty())
            popup_id = "##confirm";

        auto& dlg = m_confirms[popup_id];
        dlg.popup_id = std::move(popup_id);
        dlg.title = std::move(title);
        dlg.message = std::move(message);
        dlg.danger = danger;

        dlg.result = ConfirmResult::None;
        dlg.on_accept = std::move(on_accept);
        dlg.on_reject = std::move(on_reject);
        dlg.accept_button_text = std::move(accept_button_text);
        dlg.reject_button_text = std::move(reject_button_text);

        dlg.open = true;
    }

    void NotificationCenter::render(float dt) {
        dt = std::max(0.0f, dt);

        auto* imgui_layer = Application::get().get_imgui_layer();
        const auto theme = imgui_layer ? imgui_layer->get_accent_palette() : Honey::ImGuiLayer::UIAccentPalette{};

        // ---- Toasts: update timers ----
        for (auto& t : m_toasts)
            t.time_remaining -= dt;

        m_toasts.erase(
            std::remove_if(m_toasts.begin(), m_toasts.end(),
                           [](const Toast& t) { return t.time_remaining <= 0.0f; }),
            m_toasts.end()
        );

        // ---- Toasts: draw overlay stack ----
        ImGuiViewport* vp = ImGui::GetMainViewport();

        // Use WorkPos/WorkSize so we don't overlap the OS taskbar/menu bars in multi-viewport mode.
        const ImVec2 work_pos  = vp->WorkPos;
        const ImVec2 work_size = vp->WorkSize;

        const float pad = 10.0f;
        const float max_width = 420.0f;

        ImVec2 anchor = ImVec2(work_pos.x + work_size.x - pad, work_pos.y + pad);
        float y = anchor.y;

        // Ensure toast windows are always on top.
        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoMove;

        for (size_t i = 0; i < m_toasts.size(); ++i) {
            const Toast& t = m_toasts[i];

            const float fade_window = 0.40f;
            float alpha = 1.0f;
            if (t.time_remaining < fade_window)
                alpha = clamp01(t.time_remaining / fade_window);

            ImGui::SetNextWindowBgAlpha(0.90f * alpha);
            ImGui::SetNextWindowViewport(vp->ID);

            ImGui::SetNextWindowPos(ImVec2(anchor.x, y), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
            ImGui::SetNextWindowSizeConstraints(ImVec2(220.0f, 0.0f), ImVec2(max_width, 2000.0f));

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);

            const std::string window_id = "##toast_" + std::to_string(i);
            if (ImGui::Begin(window_id.c_str(), nullptr, flags)) {
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * alpha);

                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 p0 = ImGui::GetWindowPos();
                ImVec2 p1 = ImVec2(p0.x + 4.0f, p0.y + ImGui::GetWindowSize().y);
                dl->AddRectFilled(p0, p1, ImGui::ColorConvertFloat4ToU32(toast_accent_color(t.type)));

                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 6.0f);

                if (!t.title.empty())
                    ImGui::TextUnformatted(t.title.c_str());
                else
                    ImGui::TextUnformatted(toast_type_label(t.type));

                if (!t.message.empty()) {
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 6.0f);
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + max_width - 28.0f);
                    ImGui::TextUnformatted(t.message.c_str());
                    ImGui::PopTextWrapPos();
                }

                ImGui::PopStyleVar(); // Alpha
            }
            ImGui::End();

            ImGui::PopStyleVar(2);

            // Stable stacking increment (avoid relying on GetItemRectSize)
            y += estimate_toast_height(!t.title.empty(), !t.message.empty()) + 6.0f;
        }

        for (auto& [id, dlg] : m_confirms) {
            if (!dlg.open)
                continue;

            const std::string visible_title = dlg.title.empty() ? "Confirm" : dlg.title;
            const std::string popup_label = visible_title + "###" + dlg.popup_id;

            ImGui::OpenPopup(popup_label.c_str());

            ImGui::SetNextWindowViewport(vp->ID);
            ImGui::SetNextWindowPos(
                ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                ImGuiCond_Appearing,
                ImVec2(0.5f, 0.5f)
            );

            bool open_flag = true;
            ImGuiWindowFlags modal_flags =
                ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoDocking;

            if (ImGui::BeginPopupModal(popup_label.c_str(), &open_flag, modal_flags)) {

                if (!dlg.message.empty()) {
                    ImGui::PushTextWrapPos(520.0f);
                    ImGui::TextUnformatted(dlg.message.c_str());
                    ImGui::PopTextWrapPos();
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                const std::string reject_text = dlg.reject_button_text.empty() ? "Cancel" : dlg.reject_button_text;
                const std::string accept_text = dlg.accept_button_text.empty() ? (dlg.danger ? "Delete" : "OK") : dlg.accept_button_text;

                // Dynamic button sizing with sane clamps
                const float min_button_w = 90.0f;
                const float max_button_w = 240.0f;

                float reject_w = button_width_for_label(reject_text, min_button_w, max_button_w);
                float accept_w = button_width_for_label(accept_text, min_button_w, max_button_w);

                // If the labels are *still* too long, truncate display text to fit max width (tooltip shows full)
                const float max_text_px = max_button_w - ImGui::GetStyle().FramePadding.x * 2.0f;
                const std::string reject_display = ellipsize_to_width(reject_text, max_text_px);
                const std::string accept_display = ellipsize_to_width(accept_text, max_text_px);

                // Recompute widths based on what we'll actually display (so the button matches the visible label)
                reject_w = button_width_for_label(reject_display, min_button_w, max_button_w);
                accept_w = button_width_for_label(accept_display, min_button_w, max_button_w);

                const float spacing = ImGui::GetStyle().ItemSpacing.x;
                const float total_w = reject_w + spacing + accept_w;

                ImGui::SetCursorPosX(std::max(0.0f, ImGui::GetWindowContentRegionMax().x - total_w));

                if (ImGui::Button(reject_display.c_str(), ImVec2(reject_w, 0))) {
                    dlg.result = ConfirmResult::Rejected;
                    dlg.open = false;

                    if (dlg.on_reject)
                        dlg.on_reject();

                    ImGui::CloseCurrentPopup();
                }
                if (reject_display != reject_text && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", reject_text.c_str());
                }

                ImGui::SameLine();

                if (dlg.danger) {
                    ImGui::PushStyleColor(ImGuiCol_Button,        theme.danger_button);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.danger_button_hover);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  theme.danger_button_active);
                }

                if (ImGui::Button(accept_display.c_str(), ImVec2(accept_w, 0))) {
                    dlg.result = ConfirmResult::Accepted;
                    dlg.open = false;

                    if (dlg.on_accept)
                        dlg.on_accept();

                    ImGui::CloseCurrentPopup();
                }
                if (accept_display != accept_text && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", accept_text.c_str());
                }

                if (dlg.danger) {
                    ImGui::PopStyleColor(3);
                }

                // Handle X / Escape close
                if (!open_flag) {
                    dlg.result = ConfirmResult::Rejected;
                    dlg.open = false;

                    if (dlg.on_reject)
                        dlg.on_reject();

                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }
        }
    }

    ConfirmResult NotificationCenter::consume_confirm_result(const std::string& popup_id) {
        auto it = m_confirms.find(popup_id);
        if (it == m_confirms.end())
            return ConfirmResult::None;

        ConfirmResult r = it->second.result;
        it->second.result = ConfirmResult::None;

        // Optional: cleanup dialogs once they've been answered and closed
        if (!it->second.open && r != ConfirmResult::None) {
            // keep around if you want to re-open quickly; otherwise erase.
            m_confirms.erase(it);
        }

        return r;
    }
}