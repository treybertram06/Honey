#include "hnpch.h"
#include "opengl_renderer_api.h"

#include <glad/glad.h>

#include "opengl_buffer.h"
#include "opengl_framebuffer.h"
#include "opengl_vertex_array.h"
#include "Honey/renderer/pipeline_spec.h"

namespace Honey {
    void OpenGLRendererAPI::init() {
        HN_PROFILE_FUNCTION();

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        //glEnable(GL_DEPTH_TEST); // Not needed for 2D, in fact, it causes problems with transparency
    }


    void OpenGLRendererAPI::set_clear_color(const glm::vec4 &color) {
        glClearColor(color.r, color.g, color.b, color.a);
    }

    void OpenGLRendererAPI::set_viewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
        glViewport(x, y, width, height);
    }

    void OpenGLRendererAPI::clear() {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    std::string OpenGLRendererAPI::get_vendor() {
        const GLubyte* vendor   = glGetString(GL_VENDOR);
        const GLubyte* renderer = glGetString(GL_RENDERER);

        std::ostringstream ss;
        if (vendor) {
            ss << reinterpret_cast<const char*>(vendor);
        } else {
            ss << "Unknown Vendor";
        }

        if (renderer) {
            ss << " - " << reinterpret_cast<const char*>(renderer);
        }

        return ss.str();
    }

    uint32_t OpenGLRendererAPI::get_max_texture_slots() {
        int max_texture_units;
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &max_texture_units);
        return (uint32_t)max_texture_units;

    }

    void OpenGLRendererAPI::bind_pipeline(const Ref<Pipeline>& pipeline) {
        //no-op?
    }


    void OpenGLRendererAPI::draw_indexed(const Ref<VertexArray> &vertex_array, uint32_t index_count) {
        //uint32_t count = index_count ? vertex_array->get_index_buffer()->get_count() : index_count;
        uint32_t count = index_count ? index_count : vertex_array->get_index_buffer()->get_count();
        //HN_CORE_INFO("Index count: {0}", count);
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
    }

    void OpenGLRendererAPI::draw_indexed_instanced(const Ref<VertexArray> &vertex_array, uint32_t index_count, uint32_t instance_count) {
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

    void OpenGLRendererAPI::set_depth_write(bool mode) {
        if (mode)
            glDepthMask(GL_TRUE);
        else
            glDepthMask(GL_FALSE);
    }

    void OpenGLRendererAPI::set_blend(bool mode) {
        if (mode) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glBlendEquation(GL_FUNC_ADD);
        } else
            glDisable(GL_BLEND);
    }

    void OpenGLRendererAPI::set_blend_for_attachment(uint32_t attachment, bool mode) {
#if !defined(HN_PLATFORM_APPLE)
        if (mode)
            glEnablei(GL_BLEND, attachment);
        else
            glDisablei(GL_BLEND, attachment);
#else
        if (index == 0) {
            if (enabled)
                glEnable(GL_BLEND);
            else
                glDisable(GL_BLEND);
        }
#endif
    }

    void OpenGLRendererAPI::set_vsync(bool mode) {
        //glfwMakeContextCurrent(m_window); // ensure context current
        glfwSwapInterval(mode ? 1 : 0);
    }

    void OpenGLRendererAPI::set_cull_mode(CullMode mode) {
        glEnable(GL_CULL_FACE);
        switch (mode) {
            case CullMode::None:
                glDisable(GL_CULL_FACE);
                break;
            case CullMode::Front:
            case CullMode::Back:
                glCullFace(mode == CullMode::Front ? GL_FRONT : GL_BACK);
        }
    }

    Ref<VertexBuffer> OpenGLRendererAPI::create_vertex_buffer(uint32_t size) {
        return CreateRef<OpenGLVertexBuffer>(size);
    }

    Ref<VertexBuffer> OpenGLRendererAPI::create_vertex_buffer(float* vertices, uint32_t size) {
        return CreateRef<OpenGLVertexBuffer>(vertices, size);
    }

    Ref<IndexBuffer> OpenGLRendererAPI::create_index_buffer_u32(uint32_t* indices, uint32_t size) {
        return CreateRef<OpenGLIndexBuffer>(indices, size);
    }

    Ref<IndexBuffer> OpenGLRendererAPI::create_index_buffer_u16(uint16_t* indices, uint32_t size) {
        return CreateRef<OpenGLIndexBuffer>(indices, size);
    }

    Ref<VertexArray> OpenGLRendererAPI::create_vertex_array() {
        return CreateRef<OpenGLVertexArray>();
    }

    Ref<UniformBuffer> OpenGLRendererAPI::create_uniform_buffer(uint32_t size, uint32_t binding) {
        return CreateRef<OpenGLUniformBuffer>(size, binding);
    }

    Ref<Framebuffer> OpenGLRendererAPI::create_framebuffer(const FramebufferSpecification& spec) {
        return CreateRef<OpenGLFramebuffer>(spec);
    }
}



