#pragma once
#include <string>
#include <unordered_map>
#include <vector>

namespace Honey::UI {
    enum class ToastType { Info, Success, Warning, Error };

    struct Toast {
        ToastType type = ToastType::Info;
        std::string title;
        std::string message;
        float time_remaining = 3.0f;
    };

    enum class ConfirmResult { None, Accepted, Rejected };

    struct ConfirmDialog {
        bool open = false;
        std::string popup_id;   // must be stable while open
        std::string title;
        std::string message;

        std::string accept_button_text;
        std::string reject_button_text;

        bool danger = false;

        ConfirmResult result = ConfirmResult::None;

        std::function<void()> on_accept;
        std::function<void()> on_reject;
    };

    class NotificationCenter {
    public:
        void push_toast(ToastType type, std::string title, std::string message, float seconds = 3.0f);
        void open_confirm(std::string popup_id, std::string title, std::string message, bool danger = false);

        void open_confirm(std::string popup_id,
                          std::string title,
                          std::string message,
                          bool danger,
                          std::function<void()> on_accept,
                          std::function<void()> on_reject = {},
                          std::string accept_button_text = "OK",
                          std::string reject_button_text = "Cancel");

        void render(float dt);

        ConfirmResult consume_confirm_result(const std::string& popup_id);

    private:
        std::vector<Toast> m_toasts;
        std::unordered_map<std::string, ConfirmDialog> m_confirms;
    };
}
