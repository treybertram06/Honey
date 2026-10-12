#include "log.h"
#include "notify.h"
#include "console_buffer.h"

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


        // HN_*_WARN/ERROR/FATAL emit "[function] message". Split the prefix off (brackets may nest in template signatures).
        void split_function_prefix(ConsoleEntry& entry) {
            if (entry.level < spdlog::level::warn || entry.text.size() < 2 || entry.text[0] != '[')
                return;
            int depth = 0;
            for (size_t i = 0; i < entry.text.size(); ++i) {
                if (entry.text[i] == '[') ++depth;
                else if (entry.text[i] == ']' && --depth == 0) {
                    if (i + 1 < entry.text.size() && entry.text[i + 1] == ' ') {
                        entry.function = entry.text.substr(1, i - 1);
                        entry.text.erase(0, i + 2);
                    }
                    return;
                }
            }
        }

        // Records every log line into the ConsoleBuffer for the editor console panel.
        class ConsoleSink final : public spdlog::sinks::base_sink<std::mutex> {
        protected:
            void sink_it_(const spdlog::details::log_msg& msg) override {
                ConsoleEntry entry;
                entry.level = msg.level;
                entry.logger.assign(msg.logger_name.data(), msg.logger_name.size());
                entry.text.assign(msg.payload.data(), msg.payload.size());
                split_function_prefix(entry);
                entry.time = msg.time;
                ConsoleBuffer::push(std::move(entry));
            }

            void flush_() override {}
        };
    }

    void Log::init() {
        spdlog::set_pattern("%^[%T] %n: %v%$");
        s_core_logger = spdlog::stdout_color_mt("HONEY");
        s_core_logger->set_level(spdlog::level::trace);
        s_core_logger->sinks().push_back(std::make_shared<NotifySink>());
        s_core_logger->sinks().push_back(std::make_shared<ConsoleSink>());

        s_client_logger = spdlog::stdout_color_mt("APP");
        s_client_logger->set_level(spdlog::level::trace);
        s_client_logger->sinks().push_back(std::make_shared<ConsoleSink>());

    }


}
