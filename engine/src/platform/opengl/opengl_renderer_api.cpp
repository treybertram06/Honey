#include "hnpch.h"
#include "opengl_renderer_api.h"

#include <glad/glad.h>

namespace Honey {
    void OpenGLRendererAPI::init() {
        HN_PROFILE_FUNCTION();

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glEnable(GL_DEPTH_TEST);
    }


    void OpenGLRendererAPI::set_clear_color(const glm::vec4 &color) {
        glClearColor(color.r, color.g, color.b, color.a);
    }

    void OpenGLRendererAPI::set_viewport(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height) {
        glViewport(x, y, width, height);
    }

    void OpenGLRendererAPI::clear() {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    std::uint32_t OpenGLRendererAPI::get_max_texture_slots() {
        int max_texture_units;
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &max_texture_units);
        return (std::uint32_t)max_texture_units;

    }


    void OpenGLRendererAPI::draw_indexed(const Ref<VertexArray> &vertex_array, std::uint32_t index_count) {
        //std::uint32_t count = index_count ? vertex_array->get_index_buffer()->get_count() : index_count;
        std::uint32_t count = index_count ? index_count : vertex_array->get_index_buffer()->get_count();
        //HN_CORE_INFO("Index count: {0}", count);
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
    }

    void OpenGLRendererAPI::draw_indexed_instanced(const Ref<VertexArray> &vertex_array, std::uint32_t index_count, std::uint32_t instance_count) {
        HN_PROFILE_FUNCTION();

        vertex_array->bind();

        // If the caller passed 0 let the VAO tell us how many indices it holds
        glDrawElementsInstanced(
            GL_TRIANGLES,                // mode
            static_cast<GLsizei>(index_count), // indices per *base* mesh (6 for a quad)
            GL_UNSIGNED_INT,             // type of your IBO
            nullptr,                     // offset into IBO
            static_cast<GLsizei>(instance_count));  // how many quads to draw
    }

    void OpenGLRendererAPI::set_wireframe(bool mode) {
        if (mode)
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        else
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    void OpenGLRendererAPI::set_depth_test(bool mode) {
        if (mode)
            glEnable(GL_DEPTH_TEST);
        else
            glDisable(GL_DEPTH_TEST);
    }

    void OpenGLRendererAPI::set_blend(bool mode) {
        if (mode)
            glEnable(GL_BLEND);
        else
            glDisable(GL_BLEND);
    }



}



