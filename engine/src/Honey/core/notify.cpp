#include "hnpch.h"
#include "notify.h"

#include <mutex>

#include "log.h"

namespace Honey {

    namespace {
        std::mutex s_mutex;
        std::vector<NotifyMessage> s_queue;
        thread_local bool t_pushing = false;

        void enqueue(NotifyMessage&& msg) {
            std::lock_guard lock(s_mutex);
            for (auto& q : s_queue) {
                if (q.severity == msg.severity && q.title == msg.title && q.message == msg.message) {
                    q.count += msg.count;
                    return;
                }
            }
            // Bound memory if the editor isn't draining (e.g. headless runtime).
            if (s_queue.size() >= 256)
                s_queue.erase(s_queue.begin());
            s_queue.emplace_back(std::move(msg));
        }
    }

    bool Notify::is_pushing() { return t_pushing; }

    void Notify::push(NotifyMessage msg) {
        if (auto& logger = Log::get_core_logger()) {
            t_pushing = true;
            const std::string line = msg.title.empty() ? msg.message : msg.title + ": " + msg.message;
            switch (msg.severity) {
                case NotifySeverity::error:   logger->error("{}", line); break;
                case NotifySeverity::warning: logger->warn("{}", line); break;
                default:                      logger->info("{}", line); break;
            }
            t_pushing = false;
        }
        enqueue(std::move(msg));
    }

    void Notify::push(NotifySeverity severity, std::string title, std::string message, std::string source) {
        NotifyMessage m;
        m.severity = severity;
        m.title = std::move(title);
        m.message = std::move(message);
        m.source = std::move(source);
        push(std::move(m));
    }

    void Notify::push_from_log(NotifySeverity severity, std::string title, std::string message) {
        NotifyMessage m;
        m.severity = severity;
        m.title = std::move(title);
        m.message = std::move(message);
        enqueue(std::move(m));
    }

    std::vector<NotifyMessage> Notify::drain() {
        std::lock_guard lock(s_mutex);
        std::vector<NotifyMessage> out;
        out.swap(s_queue);
        return out;
    }

} // namespace Honey
