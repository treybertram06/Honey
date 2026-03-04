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
    };
}