#include "hnpch.h"
#include "renderer.h"
#include "renderer_2d.h"
#include "renderer_3d.h"
#include "texture_cache.h"
#include "Honey/core/engine.h"
#include "platform/opengl/opengl_shader.h"
#include "platform/vulkan/vk_context.h"
#include "platform/vulkan/vk_framebuffer.h"

namespace Honey {

    Renderer::SceneData* Renderer::m_scene_data = new Renderer::SceneData;
    std::unique_ptr<ShaderCache> Renderer::m_shader_cache = nullptr;
    Ref<Framebuffer> Renderer::s_current_target = nullptr;
    bool Renderer::s_pass_open = false;

    void Renderer::init() {
        HN_PROFILE_FUNCTION();

        auto cache_dir = std::filesystem::current_path() / "assets" / "cache" / "shaders";
        m_shader_cache = std::make_unique<ShaderCache>(cache_dir);

        RenderCommand::init();

        switch (get_api()) {
        case RendererAPI::API::opengl:
            Renderer2D::init(std::move(m_shader_cache));
            //Renderer3D::init();
            break;

        case RendererAPI::API::vulkan:
            Renderer2D::init(std::move(m_shader_cache));
            //HN_CORE_INFO("Renderer::init - Vulkan selected (skipping Renderer2D/3D init for now).");
            break;

        case RendererAPI::API::none:
        default:
            HN_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
            break;
        }

    }

    void Renderer::shutdown() {
        HN_PROFILE_FUNCTION();

        switch (get_api()) {
        case RendererAPI::API::opengl:
            Renderer2D::shutdown();
            //Renderer3D::init();
            break;

        case RendererAPI::API::vulkan:
            Renderer2D::shutdown();
            //HN_CORE_INFO("Renderer::init - Vulkan selected (skipping Renderer2D/3D init for now).");
            break;

        case RendererAPI::API::none:
        default:
            HN_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
            break;
        }

        TextureCache::get().clear();

        if (RendererAPI::get_api() == RendererAPI::API::vulkan) {
            auto* ctx = Application::get().get_window().get_context();
            if (ctx) {
                ctx->wait_idle();
            }
        }

        RenderCommand::shutdown();

        delete m_scene_data;
        m_scene_data = nullptr;
        m_shader_cache.reset();
        s_current_target.reset();
        s_pass_open = false;
    }

    void Renderer::on_window_resize(uint32_t width, uint32_t height) {
        RenderCommand::set_viewport(0, 0, width, height);
    }

    void Renderer::begin_frame() {
        HN_PROFILE_FUNCTION();

        if (get_api() != RendererAPI::API::vulkan)
            return;

        auto* base = Application::get().get_window().get_context();
        auto* vk = dynamic_cast<VulkanContext*>(base);
        HN_CORE_ASSERT(vk, "Renderer::begin_frame() expected VulkanContext when Vulkan is active");

        vk->frame_packet().begin_frame();
    }

    void Renderer::set_render_target(const Ref<Framebuffer>& framebuffer) {
        // Changing target while a pass is open is a misuse; assert in debug.
        HN_CORE_ASSERT(!s_pass_open, "Renderer::set_render_target called while a pass is open. Call end_pass() first.");
        s_current_target = framebuffer;
    }

    void Renderer::begin_pass() {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(!s_pass_open, "Renderer::begin_pass called while another pass is already open.");
        s_pass_open = true;

        switch (get_api()) {
        case RendererAPI::API::opengl: {
            // For GL, just bind the FBO if there is one, otherwise bind default.
            if (s_current_target) {
                s_current_target->bind();
            } else {
                // Default framebuffer: relying on GL context default FBO 0.
                // If you have a helper for this, call it here instead.
                // No explicit call is strictly necessary if your GL backend assumes FBO 0 when no FB is bound.
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }
            break;
        }
        case RendererAPI::API::vulkan: {
            auto* base = Application::get().get_window().get_context();
            auto* vk = dynamic_cast<VulkanContext*>(base);
            HN_CORE_ASSERT(vk, "Renderer::begin_pass (Vulkan) expected VulkanContext");

            auto& pkt = vk->frame_packet();
            HN_CORE_ASSERT(pkt.frame_begun, "Renderer::begin_pass (Vulkan): frame not begun. Call Renderer::begin_frame() first.");

            // Push a "begin pass" command into the packet.
            if (!s_current_target) {
                // Swapchain/main-window pass (what used to be BeginSwapchainPass).
                VulkanContext::FramePacket::Cmd cmd{};
                cmd.type = VulkanContext::FramePacket::CmdType::BeginSwapchainPass;
                cmd.begin.clearColor = pkt.clearColor;
                pkt.cmds.push_back(cmd);
            } else {
                // Offscreen pass: let VulkanContext know which framebuffer to use.
                auto* vk_fb = dynamic_cast<VulkanFramebuffer*>(s_current_target.get());
                HN_CORE_ASSERT(vk_fb, "Renderer::begin_pass (Vulkan): current target is not a VulkanFramebuffer");

                VulkanContext::FramePacket::Cmd cmd{};
                cmd.type = VulkanContext::FramePacket::CmdType::BeginOffscreenPass;
                cmd.offscreen.framebuffer = vk_fb;
                cmd.offscreen.clearColor = pkt.clearColor;
                pkt.cmds.push_back(cmd);
            }
            break;
        }
        case RendererAPI::API::none:
        default:
            HN_CORE_ASSERT(false, "Renderer::begin_pass: unsupported RendererAPI");
            break;
        }
    }

    void Renderer::end_pass() {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(s_pass_open, "Renderer::end_pass called with no open pass.");
        s_pass_open = false;

        switch (get_api()) {
        case RendererAPI::API::opengl: {
            // For GL, unbind offscreen FBO if one was bound.
            if (s_current_target) {
                s_current_target->unbind();
            } else {
                // Leaving default framebuffer bound is fine; nothing to do.
            }
            break;
        }
        case RendererAPI::API::vulkan: {
            auto* base = Application::get().get_window().get_context();
            auto* vk = dynamic_cast<VulkanContext*>(base);
            HN_CORE_ASSERT(vk, "Renderer::end_pass (Vulkan) expected VulkanContext");

            auto& pkt = vk->frame_packet();
            HN_CORE_ASSERT(pkt.frame_begun, "Renderer::end_pass (Vulkan): frame not begun.");

            VulkanContext::FramePacket::Cmd cmd{};
            cmd.type = VulkanContext::FramePacket::CmdType::EndPass;
            pkt.cmds.push_back(cmd);
            break;
        }
        case RendererAPI::API::none:
        default:
            HN_CORE_ASSERT(false, "Renderer::end_pass: unsupported RendererAPI");
            break;
        }
    }

    void Renderer::begin_scene(OrthographicCamera& camera) {
        m_scene_data->view_projection_matrix = camera.get_view_projection_matrix();
    }

    void Renderer::end_scene() {
    }

   void Renderer::submit(const Ref<Shader>& shader, const Ref<VertexArray> &vertex_array, const glm::mat4& transform) {
        if (get_api() != RendererAPI::API::opengl) {
            HN_CORE_ASSERT(false, "Renderer::submit is OpenGL-only right now. Guard or implement Vulkan path before calling.");
            return;
        }

        shader->bind();

        auto gl_shader = std::dynamic_pointer_cast<OpenGLShader>(shader);
        HN_CORE_ASSERT(gl_shader, "Renderer::submit expected an OpenGLShader when OpenGL API is selected.");

        gl_shader->upload_uniform_mat4("u_view_projection", m_scene_data->view_projection_matrix);
        gl_shader->upload_uniform_mat4("u_transform", transform);

        vertex_array->bind();
        RenderCommand::draw_indexed(vertex_array);
    }

}
