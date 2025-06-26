#pragma once
#include "hnpch.h"

namespace Honey {

    struct ProfileResult {
        const char* name;
        float time;
    };

    class FramerateCounter {
    public:
        FramerateCounter(size_t history_size = 500) // 500 frames default
            : m_history_size(history_size) {
            m_frame_times.reserve(history_size);
        }

        void update(float delta_time) {
            if (delta_time <= 0.0f) return;

            float fps = 1.0f / delta_time;
            m_frame_times.push_back(fps);

            // maintain rolling history
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
            if (m_frame_times.size() < 100) return 0.0f; // not enough data
            std::vector<float> sorted = m_frame_times;
            std::sort(sorted.begin(), sorted.end());
            size_t idx = static_cast<size_t>(sorted.size() * 0.01f);
            return sorted[idx];
        }

        float get_0_1_percent_low() const {
            if (m_frame_times.size() < 1000) return 0.0f; // not enough data
            std::vector<float> sorted = m_frame_times;
            std::sort(sorted.begin(), sorted.end());
            size_t idx = static_cast<size_t>(sorted.size() * 0.001f);
            return sorted[idx];
        }

    private:
        float m_time_accumulator = 0.0f;
        int m_frame_count = 0;
        int m_smoothed_fps = 0;
        float m_update_interval = 0.5f;

        std::vector<float> m_frame_times;
        size_t m_history_size;
    };

    template<typename Fn>
    class ScopedTimer {
    public:
        ScopedTimer(const char* label, Fn&& func)
            : m_label(label), m_start(std::chrono::high_resolution_clock::now()), m_func(func) {}

        ~ScopedTimer() {
            auto end = std::chrono::high_resolution_clock::now();
            float ms = std::chrono::duration<float, std::milli>(end - m_start).count();
            m_func({m_label, ms});
        }

    private:
        const char* m_label;
        std::chrono::high_resolution_clock::time_point m_start;
        Fn m_func;
    };

}