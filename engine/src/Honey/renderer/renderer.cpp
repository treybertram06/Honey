#include "hnpch.h"
#include "renderer.h"
#include "renderer_2d.h"
#include "renderer_3d/renderer_3d.h"
#include "texture_cache.h"
#include "Honey/core/engine.h"
#include "platform/opengl/opengl_shader.h"
#include "platform/vulkan/vk_context.h"
#include "platform/vulkan/vk_framebuffer.h"
#include "platform/vulkan/vk_backend.h"
#include "imgui.h"

namespace Honey {

    Renderer::SceneData* Renderer::m_scene_data = new Renderer::SceneData;
    Ref<ShaderCache> Renderer::m_shader_cache = nullptr;
    Ref<Framebuffer> Renderer::s_current_target = nullptr;
    bool Renderer::s_pass_open = false;

    Ref<ShaderCache> Renderer::get_shader_cache() {
        HN_CORE_ASSERT(m_shader_cache, "Renderer::shader_cache() called before Renderer::init()");
        return m_shader_cache;
    }

    void Renderer::init() {
        HN_PROFILE_FUNCTION();

        m_shader_cache = CreateRef<ShaderCache>();

        RenderCommand::init();

        switch (get_api()) {
        case RendererAPI::API::opengl:
            Renderer2D::init();
            Renderer3D::init();
            break;

        case RendererAPI::API::vulkan:
            Renderer2D::init();
            Renderer3D::init();
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
            Renderer3D::shutdown();
            break;

        case RendererAPI::API::vulkan:
            Renderer2D::shutdown();
            Renderer3D::shutdown();
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

    static bool s_pipelines_prewarmed = false;
    void Renderer::begin_frame() {
        HN_PROFILE_FUNCTION();

        if (get_api() != RendererAPI::API::vulkan)
            return;

        auto* base = Application::get().get_window().get_context();
        auto* vk = dynamic_cast<VulkanContext*>(base);
        HN_CORE_ASSERT(vk, "Renderer::begin_frame() expected VulkanContext when Vulkan is active");

        vk->begin_frame_recording();

        if (!s_pipelines_prewarmed) {
            prewarm_pipelines(nullptr);
            s_pipelines_prewarmed = true;
        }
    }

    void Renderer::set_render_target(const Ref<Framebuffer>& framebuffer) {
        // Changing target while a pass is open is a misuse; assert in debug.
        HN_CORE_ASSERT(!s_pass_open, "Renderer::set_render_target called while a pass is open. Call end_pass() first.");
        s_current_target = framebuffer;
    }

    void Renderer::begin_pass(const char* name) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(!s_pass_open, "Renderer::begin_pass called while another pass is already open.");
        s_pass_open = true;

        switch (get_api()) {
        case RendererAPI::API::opengl: {
            if (s_current_target) {
                s_current_target->bind();
            } else {
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }
            break;
        }
        case RendererAPI::API::vulkan: {
            auto* base = Application::get().get_window().get_context();
            auto* vk = dynamic_cast<VulkanContext*>(base);
            HN_CORE_ASSERT(vk, "Renderer::begin_pass (Vulkan) expected VulkanContext");
            HN_CORE_ASSERT(vk->is_recording(), "Renderer::begin_pass (Vulkan): no active recording. Call Renderer::begin_frame() first.");

            VkCommandBuffer cmd = vk->get_recording_cmd();
            const glm::vec4 clear_color = vk->get_clear_color();

            if (!s_current_target) {
                // Swapchain pass
                vk->cmd_begin_debug_label(cmd, name ? name : "SwapchainPass", 0.2f, 0.6f, 1.0f);

                VkClearValue clear_values[2]{};
                clear_values[0].color = { { clear_color.r, clear_color.g, clear_color.b, clear_color.a } };
                clear_values[1].depthStencil.depth = 1.0f;
                clear_values[1].depthStencil.stencil = 0;

                const uint32_t img_idx = vk->get_recording_image_index();
                VkRenderPassBeginInfo rp_begin{};
                rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                rp_begin.renderPass  = vk->get_render_pass();
                rp_begin.framebuffer = vk->get_swapchain_framebuffer(img_idx);
                rp_begin.renderArea.offset = { 0, 0 };
                rp_begin.renderArea.extent = { vk->get_swapchain_extent_width(),
                                               vk->get_swapchain_extent_height() };
                rp_begin.clearValueCount = 2;
                rp_begin.pClearValues = clear_values;

                vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
                vk->open_render_pass(true, { vk->get_swapchain_extent_width(),
                                             vk->get_swapchain_extent_height() });
            } else {
                // Offscreen pass
                auto* vk_fb = dynamic_cast<VulkanFramebuffer*>(s_current_target.get());
                HN_CORE_ASSERT(vk_fb, "Renderer::begin_pass (Vulkan): current target is not a VulkanFramebuffer");

                vk->cmd_begin_debug_label(cmd, name ? name : "OffscreenPass", 1.0f, 0.6f, 0.2f);

                VkRenderPass  rp     = reinterpret_cast<VkRenderPass>(vk_fb->get_render_pass());
                VkFramebuffer fb     = reinterpret_cast<VkFramebuffer>(vk_fb->get_framebuffer());
                auto          extent = vk_fb->get_extent();

                const uint32_t colorCount       = vk_fb->get_color_attachment_count();
                const bool     hasDepth         = vk_fb->has_depth_attachment();
                const uint32_t totalAttachments = colorCount + (hasDepth ? 1u : 0u);

                std::vector<VkClearValue> clear_values(totalAttachments);
                for (uint32_t i = 0; i < colorCount; ++i) {
                    const auto fmt = vk_fb->get_color_attachment_format(i);
                    if (i == 0) {
                        clear_values[i].color = { { clear_color.r, clear_color.g, clear_color.b, clear_color.a } };
                    } else {
                        switch (fmt) {
                        case FramebufferTextureFormat::RED_INTEGER:
                            clear_values[i].color.int32[0] = -1;
                            clear_values[i].color.int32[1] = -1;
                            clear_values[i].color.int32[2] = -1;
                            clear_values[i].color.int32[3] = -1;
                            break;
                        default:
                            clear_values[i].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
                            break;
                        }
                    }
                }
                if (hasDepth) {
                    clear_values[colorCount].depthStencil.depth   = 1.0f;
                    clear_values[colorCount].depthStencil.stencil = 0;
                }

                VkRenderPassBeginInfo rp_begin{};
                rp_begin.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                rp_begin.renderPass      = rp;
                rp_begin.framebuffer     = fb;
                rp_begin.renderArea.offset = { 0, 0 };
                rp_begin.renderArea.extent = { extent.width, extent.height };
                rp_begin.clearValueCount = totalAttachments;
                rp_begin.pClearValues    = clear_values.data();

                vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
                vk->open_render_pass(false, { extent.width, extent.height });
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
            if (s_current_target)
                s_current_target->unbind();
            break;
        }
        case RendererAPI::API::vulkan: {
            auto* base = Application::get().get_window().get_context();
            auto* vk = dynamic_cast<VulkanContext*>(base);
            HN_CORE_ASSERT(vk, "Renderer::end_pass (Vulkan) expected VulkanContext");
            HN_CORE_ASSERT(vk->is_recording(), "Renderer::end_pass (Vulkan): no active recording.");

            if (!vk->is_render_pass_open())
                break;

            VkCommandBuffer cmd = vk->get_recording_cmd();

            // Render ImGui before closing the swapchain pass.
            if (vk->is_current_pass_swapchain()) {
                auto& backend = Application::get().get_vulkan_backend();
                if (ImGui::GetDrawData() && ImGui::GetDrawData()->CmdListsCount > 0) {
                    const uint32_t img_idx = vk->get_recording_image_index();
                    VkExtent2D imgui_extent{ vk->get_swapchain_extent_width(),
                                            vk->get_swapchain_extent_height() };
                    VkImageView imgui_target_view = vk->get_swapchain_image_view(img_idx);
                    backend.render_imgui_on_current_swapchain_image(cmd, imgui_target_view, imgui_extent);
                }
            }

            vkCmdEndRenderPass(cmd);
            vk->cmd_end_debug_label(cmd);

            vk->close_render_pass();
            break;
        }
        case RendererAPI::API::none:
        default:
            HN_CORE_ASSERT(false, "Renderer::end_pass: unsupported RendererAPI");
            break;
        }
    }

    void Renderer::prewarm_pipelines(void* native_render_pass) {
        HN_PROFILE_FUNCTION();

        // If caller didn't pass a pass, we can default to the main swapchain pass on Vulkan.
        if (get_api() == RendererAPI::API::vulkan && !native_render_pass) {
            auto* base = Application::get().get_window().get_context();
            auto* vk = dynamic_cast<VulkanContext*>(base);
            HN_CORE_ASSERT(vk, "Renderer::prewarm_pipelines: expected VulkanContext");
            native_render_pass = vk->get_render_pass();
        }

        Renderer3D::prewarm_pipelines(native_render_pass);
        Renderer2D::prewarm_pipelines(native_render_pass);
        HN_CORE_INFO("Renderer::prewarm_pipelines: done.");
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
