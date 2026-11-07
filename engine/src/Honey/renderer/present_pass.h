#pragma once
#include "framebuffer.h"
#include "renderer.h"

namespace Honey {

    class PresentPass {
    public:
        static void init();
        static void shutdown();
        static bool is_initialized();

        static void record(const Ref<Framebuffer>& src, PresentFit fit);

    };

}
