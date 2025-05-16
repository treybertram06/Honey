#pragma once

namespace Honey {
    enum class RendererAPI {
        none = 0, opengl = 1
    };

    class Renderer {
    public:

        inline static RendererAPI get_api() { return s_renderer_api; }

    private:

        static RendererAPI s_renderer_api;

    };

}