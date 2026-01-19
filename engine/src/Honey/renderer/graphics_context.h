#pragma once

namespace Honey {

    class GraphicsContext {
    public:
        virtual void init() = 0;
        virtual void swap_buffers() = 0;
        virtual void wait_idle() = 0;
    };
}