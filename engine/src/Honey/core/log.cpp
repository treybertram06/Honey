#include "log.h"
#include "notify.h"

#include <chrono>
#include <spdlog/sinks/base_sink.h>

namespace Honey {

    std::shared_ptr<spdlog::logger> Log::s_core_logger;
    std::shared_ptr<spdlog::logger> Log::s_client_logger;

    namespace {
        // Forwards error-level core logs to the Notify queue, rate limited so a failure storm can't flood the editor.
        class NotifySink final : public spdlog::sinks::base_sink<std::mutex> {
        protected:
            void sink_it_(const spdlog::details::log_msg& msg) override {
                if (msg.level < spdlog::level::err || Notify::is_pushing())
                    return;

                using clock = std::chrono::steady_clock;
                const auto now = clock::now();
                if (now - m_window_start >= std::chrono::seconds(1)) {
                    if (m_suppressed > 0) {
                        Notify::push_from_log(NotifySeverity::error, "More errors",
                                              std::to_string(m_suppressed) + " more errors, see console.");
                    }
                    m_window_start = now;
                    m_window_count = 0;
                    m_suppressed = 0;
                }

                if (m_window_count >= 5) {
                    ++m_suppressed;
                    return;
                }
                ++m_window_count;

                Notify::push_from_log(NotifySeverity::error, "Error",
                                      std::string(msg.payload.data(), msg.payload.size()));
            }

            void flush_() override {}

        private:
            std::chrono::steady_clock::time_point m_window_start{};
            int m_window_count = 0;
            int m_suppressed = 0;
        };
    }

    void Log::init() {
        spdlog::set_pattern("%^[%T] %n: %v%$");
        s_core_logger = spdlog::stdout_color_mt("HONEY");
        s_core_logger->set_level(spdlog::level::trace);
        s_core_logger->sinks().push_back(std::make_shared<NotifySink>());

        s_client_logger = spdlog::stdout_color_mt("APP");
        s_client_logger->set_level(spdlog::level::trace);

    }


}
