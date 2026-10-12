#pragma once

#include <string>
#include <vector>

#include <format>

#include "base.h"

namespace Honey {

    enum class NotifySeverity { info, success, warning, error };

    struct NotifyMessage {
        NotifySeverity severity = NotifySeverity::info;
        std::string title;
        std::string message;
        std::string source;
        int count = 1; // identical messages coalesced while queued
    };

    // Thread-safe engine -> editor notification queue. Any subsystem pushes, the editor drains once per frame.
    // Works in every build config (unlike HN_CORE_* logging, which is compiled out in release).
    class HONEY_API Notify {
    public:
        // Also writes the message to the core log.
        static void push(NotifyMessage msg);
        static void push(NotifySeverity severity, std::string title, std::string message, std::string source = {});

        // Queue only, no logging. Used by the log sink to avoid feedback loops.
        static void push_from_log(NotifySeverity severity, std::string title, std::string message);

        static std::vector<NotifyMessage> drain();

        // True while Notify::push is writing to the log (the log sink must not re-forward).
        static bool is_pushing();
    };

} // namespace Honey

#define HN_NOTIFY_INFO(title, ...)    ::Honey::Notify::push(::Honey::NotifySeverity::info,    (title), fmt::format(__VA_ARGS__), CURRENT_FUNCTION)
#define HN_NOTIFY_WARN(title, ...)    ::Honey::Notify::push(::Honey::NotifySeverity::warning, (title), fmt::format(__VA_ARGS__), CURRENT_FUNCTION)
#define HN_NOTIFY_ERROR(title, ...)   ::Honey::Notify::push(::Honey::NotifySeverity::error,   (title), fmt::format(__VA_ARGS__), CURRENT_FUNCTION)
