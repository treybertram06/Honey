#include "hnpch.h"
#include "statistics.h"

namespace Honey {

    FramerateCounter::FramerateCounter(size_t history_size = 500) // 500 frames default
        : m_history_size(history_size) {
        m_frame_times.reserve(history_size);
    }

    void FramerateCounter::update(float delta_time) {
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

    float FramerateCounter::get_average_fps() const {
        if (m_frame_times.empty()) return 0.0f;
        float sum = std::accumulate(m_frame_times.begin(), m_frame_times.end(), 0.0f);
        return sum / m_frame_times.size();
    }

    float FramerateCounter::get_min_fps() const {
        if (m_frame_times.empty()) return 0.0f;
        return *std::min_element(m_frame_times.begin(), m_frame_times.end());
    }

    float FramerateCounter::get_max_fps() const {
        if (m_frame_times.empty()) return 0.0f;
        return *std::max_element(m_frame_times.begin(), m_frame_times.end());
    }

    float FramerateCounter::get_1_percent_low() const {
        if (m_frame_times.size() < 100) return 0.0f; // not enough data
        std::vector<float> sorted = m_frame_times;
        std::sort(sorted.begin(), sorted.end());
        size_t idx = static_cast<size_t>(sorted.size() * 0.01f);
        return sorted[idx];
    }

    float FramerateCounter::get_0_1_percent_low() const {
        if (m_frame_times.size() < 1000) return 0.0f; // not enough data
        std::vector<float> sorted = m_frame_times;
        std::sort(sorted.begin(), sorted.end());
        size_t idx = static_cast<size_t>(sorted.size() * 0.001f);
        return sorted[idx];
    }


    ScopedTimer::ScopedTimer(const std::string& label)
        : m_label(label), m_start(std::chrono::high_resolution_clock::now()) {}

    ScopedTimer::~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        float ms = std::chrono::duration<float, std::milli>(end - m_start).count();
        HN_TRACE("[Timer] {0}: {1}ms", m_label, ms);
        //std::cout << "[Timer] " << m_label << ": " << ms << " ms\n";
    }

}