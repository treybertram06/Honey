#pragma once
#include "hnpch.h"

namespace Honey {

    class FramerateCounter {
    public:
        FramerateCounter(size_t history_size = 500);

        void update(float delta_time);
        int get_smoothed_fps() const { return m_smoothed_fps; }

        float get_average_fps() const;
        float get_min_fps() const;
        float get_max_fps() const;
        float get_1_percent_low() const;
        float get_0_1_percent_low() const;

    private:
        float m_time_accumulator = 0.0f;
        int m_frame_count = 0;
        int m_smoothed_fps = 0;
        float m_update_interval = 0.5f;

        std::vector<float> m_frame_times;
        size_t m_history_size;
    };

    class ScopedTimer {
    public:
        ScopedTimer(const std::string& label);
        ~ScopedTimer();

    private:
        std::string m_label;
        std::chrono::high_resolution_clock::time_point m_start;
    };

}