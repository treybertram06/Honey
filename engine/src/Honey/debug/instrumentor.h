#pragma once
#include "hnpch.h"

#include <string>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <thread>
#include <vector>
#include <numeric>

namespace Honey {

    // Callback-style profile result (for your ScopedTimer)
    struct ProfileResult {
        const char* name;
        float time;
    };

    // JSON-compatible profile event structure
    struct ProfileResultJson {
        std::string Name;
        long long Start;
        long long End;
        uint32_t ThreadID;
    };

    // Represents an active profiling session
    struct ProfileSession {
        std::string Name;
    };

    // Profiler singleton: manages session and JSON output
    class Profiler {
    public:
        static Profiler& get() {
            static Profiler instance;
            return instance;
        }

        void begin_session(const std::string& name, const std::string& filepath = "results.json") {
            m_output_stream.open(filepath);
            write_header();
            m_current_session = new ProfileSession{ name };
        }

        void end_session() {
            write_footer();
            m_output_stream.close();
            delete m_current_session;
            m_current_session = nullptr;
            m_profile_count = 0;
        }

        void write_profile(const ProfileResultJson& result) {
            if (m_profile_count++ > 0)
                m_output_stream << ',';

            std::string safe_name = result.Name;
            std::replace(safe_name.begin(), safe_name.end(), '"', '\'');

            m_output_stream << '{';
            m_output_stream << "\"cat\":\"function\",";
            m_output_stream << "\"dur\":" << (result.End - result.Start) << ',';
            m_output_stream << "\"name\":\"" << safe_name << "\",";
            m_output_stream << "\"ph\":\"X\",";
            m_output_stream << "\"pid\":0,";
            m_output_stream << "\"tid\":" << result.ThreadID << ',';
            m_output_stream << "\"ts\":" << result.Start;
            m_output_stream << '}';
            m_output_stream.flush();
        }

    private:
        Profiler() = default;

        void write_header() {
            m_output_stream << "{\"otherData\":{},\"traceEvents\":[";
            m_output_stream.flush();
        }

        void write_footer() {
            m_output_stream << "]}";
            m_output_stream.flush();
        }

        std::ofstream m_output_stream;
        ProfileSession* m_current_session = nullptr;
        int m_profile_count = 0;
    };

    // RAII timer for JSON profiling
    class ProfileTimer {
    public:
        explicit ProfileTimer(const char* name)
            : m_name(name), m_stopped(false) {
            m_start_timepoint = std::chrono::high_resolution_clock::now();
        }

        ~ProfileTimer() {
            if (!m_stopped)
                stop();
        }

        void stop() {
            auto end_timepoint = std::chrono::high_resolution_clock::now();
            long long start = std::chrono::time_point_cast<std::chrono::microseconds>(m_start_timepoint)
                                  .time_since_epoch().count();
            long long end = std::chrono::time_point_cast<std::chrono::microseconds>(end_timepoint)
                                .time_since_epoch().count();

            uint32_t thread_id = std::hash<std::thread::id>{}(std::this_thread::get_id());
            Profiler::get().write_profile({ m_name, start, end, thread_id });
            m_stopped = true;
        }

    private:
        const char* m_name;
        std::chrono::time_point<std::chrono::high_resolution_clock> m_start_timepoint;
        bool m_stopped;
    };

    // Rolling FramerateCounter (unchanged)
    class FramerateCounter {
    public:
        explicit FramerateCounter(size_t history_size = 500)
            : m_history_size(history_size) {
            m_frame_times.reserve(history_size);
        }

        void update(float delta_time) {
            if (delta_time <= 0.0f) return;
            float fps = 1.0f / delta_time;
            m_frame_times.push_back(fps);
            if (m_frame_times.size() > m_history_size)
                m_frame_times.erase(m_frame_times.begin());
            m_time_accumulator += delta_time;
            m_frame_count++;
            if (m_time_accumulator >= m_update_interval) {
                m_smoothed_fps = static_cast<int>(m_frame_count / m_time_accumulator);
                m_time_accumulator = 0.0f;
                m_frame_count = 0;
            }
        }

        int get_smoothed_fps() const { return m_smoothed_fps; }
        float get_average_fps() const {
            if (m_frame_times.empty()) return 0.0f;
            float sum = std::accumulate(m_frame_times.begin(), m_frame_times.end(), 0.0f);
            return sum / m_frame_times.size();
        }
        float get_min_fps() const {
            if (m_frame_times.empty()) return 0.0f;
            return *std::min_element(m_frame_times.begin(), m_frame_times.end());
        }
        float get_max_fps() const {
            if (m_frame_times.empty()) return 0.0f;
            return *std::max_element(m_frame_times.begin(), m_frame_times.end());
        }
        float get_1_percent_low() const {
            if (m_frame_times.size() < 100) return 0.0f;
            auto sorted = m_frame_times;
            std::sort(sorted.begin(), sorted.end());
            return sorted[static_cast<size_t>(sorted.size() * 0.01f)];
        }
        float get_0_1_percent_low() const {
            if (m_frame_times.size() < 1000) return 0.0f;
            auto sorted = m_frame_times;
            std::sort(sorted.begin(), sorted.end());
            return sorted[static_cast<size_t>(sorted.size() * 0.001f)];
        }

    private:
        float m_time_accumulator = 0.0f;
        int m_frame_count = 0;
        int m_smoothed_fps = 0;
        float m_update_interval = 0.5f;
        std::vector<float> m_frame_times;
        size_t m_history_size;
    };

    // Callback-based scoped timer (unchanged)
    template<typename Fn>
    class ScopedTimer {
    public:
        ScopedTimer(const char* label, Fn&& func)
            : m_label(label), m_start(std::chrono::high_resolution_clock::now()), m_func(std::forward<Fn>(func)) {}

        ~ScopedTimer() {
            auto end = std::chrono::high_resolution_clock::now();
            float ms = std::chrono::duration<float, std::milli>(end - m_start).count();
            m_func({ m_label, ms });
        }

    private:
        const char* m_label;
        std::chrono::time_point<std::chrono::high_resolution_clock> m_start;
        Fn m_func;
    };

} // namespace Honey

#define HN_PROFILE 1
#if HN_PROFILE
#define HN_PROFILE_BEGIN_SESSION(name, filepath) ::Honey::Profiler::get().begin_session(name, filepath)
#define HN_PROFILE_END_SESSION() ::Honey::Profiler::get().end_session()
#define HN_PROFILE_SCOPE(name) ::Honey::ProfileTimer timer##__LINE__(name);
#if defined(_MSC_VER)
#define HN_FUNCTION_SIG __FUNCSIG__
#elif defined(__clang__) || defined(__GNUC__)
#define HN_FUNCTION_SIG __PRETTY_FUNCTION__
#else
#define HN_FUNCTION_SIG __func__
#endif

#define HN_PROFILE_FUNCTION() HN_PROFILE_SCOPE(HN_FUNCTION_SIG)
#else
#define HN_PROFILE_BEGIN_SESSION(name, filepath)
#define HN_PROFILE_END_SESSION()
#define HN_PROFILE_SCOPE(name)
#define HN_PROFILE_FUNCTION()
#endif