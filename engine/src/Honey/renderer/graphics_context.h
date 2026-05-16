#pragma once

namespace Honey {

    class GraphicsContext {
    public:
        virtual ~GraphicsContext() = default;

        virtual void init() = 0;
        virtual void swap_buffers() = 0;
        virtual void wait_idle() = 0;
        virtual void refresh_all_texture_samplers() = 0;
        virtual double get_last_gpu_frame_time_ms() const { return -1.0; }
        virtual uint32_t get_gpu_zone_count() const { return 0; }
        virtual const char* get_gpu_zone_name(uint32_t slot) const { return nullptr; }
        virtual double get_gpu_zone_time_ms(uint32_t slot) const { return 0.0; }
    };
}