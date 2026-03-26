#include "hnpch.h"
#include "cloth_system.h"
#include "components.h"

#include "Honey/core/engine.h"
#include "Honey/renderer/renderer.h"
#include "Honey/renderer/frame_graph.h"
#include "Honey/renderer/frame_graph_registry.h"
#include "Honey/scene/scene.h"

#include "platform/vulkan/vk_cloth_sim.h"
#include "platform/vulkan/vk_cloth_renderer.h"
#include "platform/vulkan/vk_context.h"
#include "platform/vulkan/vk_buffer.h"

namespace Honey {

    struct ClothSystem::Impl {
        struct ClothHandle {
            std::unique_ptr<VulkanClothSim>      sim;
            std::unique_ptr<VulkanClothRenderer> renderer;
            bool seeded     = false;
            bool init_failed = false;
        };

        std::unordered_map<entt::entity, ClothHandle> handles;
        float current_dt = 0.016f;
    };

    ClothSystem::ClothSystem()
        : m_impl(std::make_unique<Impl>()) {}

    ClothSystem::~ClothSystem() = default;

    void ClothSystem::set_frame_dt(float dt) {
        m_impl->current_dt = dt;
    }

    static VulkanContext* get_vulkan_context() {
        if (RendererAPI::get_api() != RendererAPI::API::vulkan)
            return nullptr;
        auto* base_ctx = Application::get().get_window().get_context();
        return dynamic_cast<VulkanContext*>(base_ctx);
    }

    void ClothSystem::on_start(entt::registry& registry) {
        auto* vk_ctx = get_vulkan_context();
        if (!vk_ctx)
            return;

        auto view = registry.view<ClothComponent>();
        for (auto e : view) {
            const auto& comp = view.get<ClothComponent>(e);
            auto& handle = m_impl->handles[e];
            if (handle.sim || handle.init_failed)
                continue;

            handle.sim = std::make_unique<VulkanClothSim>();
            if (!handle.sim->init(vk_ctx, comp.grid_width, comp.grid_height)) {
                HN_CORE_ERROR("ClothSystem: VulkanClothSim init failed for entity {}", (uint32_t)e);
                handle.init_failed = true;
                handle.sim.reset();
                continue;
            }

            handle.renderer = std::make_unique<VulkanClothRenderer>();
            if (!handle.renderer->init(vk_ctx, comp.grid_width, comp.grid_height)) {
                HN_CORE_WARN("ClothSystem: VulkanClothRenderer init failed — cloth will not be visible");
            }

            HN_CORE_INFO("ClothSystem: initialized cloth {}x{} for entity {}", comp.grid_width, comp.grid_height, (uint32_t)e);
        }
    }

    void ClothSystem::on_stop(entt::registry& registry) {
        auto* vk_ctx = get_vulkan_context();
        if (vk_ctx) {
            // Cancel any cloth draw callbacks queued in the current frame before
            // destroying the pipelines they reference.
            vk_ctx->cancel_pending_custom_vulkan_cmds();
            vk_ctx->wait_idle();
        }

        m_impl->handles.clear();
    }

    void ClothSystem::on_render(entt::registry& registry, const glm::mat4& vp) {
        auto view = registry.view<ClothComponent>();
        for (auto e : view) {
            auto it = m_impl->handles.find(e);
            if (it == m_impl->handles.end())
                continue;

            auto& handle = it->second;
            if (!handle.sim || !handle.sim->valid())
                continue;
            if (!handle.renderer || !handle.renderer->valid())
                continue;

            handle.renderer->record_draw(handle.sim->current_read_buffer(), vp);
        }
    }

    bool ClothSystem::execute_cloth_seed(FrameGraphPassContext& ctx) {
        bool any_submitted = false;

        for (auto& [e, handle] : m_impl->handles) {
            if (!handle.sim || !handle.sim->valid() || handle.seeded)
                continue;

            const auto state_a = ctx.get_buffer("clothStateA");
            const auto state_b = ctx.get_buffer("clothStateB");

            if (state_a && state_b) {
                auto vk_a = std::dynamic_pointer_cast<VulkanStorageBuffer>(state_a);
                auto vk_b = std::dynamic_pointer_cast<VulkanStorageBuffer>(state_b);

                if (vk_a && vk_b) {
                    handle.sim->set_external_state_buffers(
                        reinterpret_cast<VkBuffer>(vk_a->get_native_buffer()),
                        reinterpret_cast<VkBuffer>(vk_b->get_native_buffer()));
                } else {
                    handle.sim->set_external_state_buffers(VK_NULL_HANDLE, VK_NULL_HANDLE);
                }
            } else {
                handle.sim->set_external_state_buffers(VK_NULL_HANDLE, VK_NULL_HANDLE);
            }

            handle.sim->reset_ping_pong();

            const bool submitted = ctx.submit_vulkan_compute([&](VkCommandBuffer cmd) {
                handle.sim->record_seed(cmd);
            });

            if (submitted) {
                handle.sim->swap_ping_pong();
                handle.seeded = true;
                any_submitted = true;
                HN_CORE_INFO("ClothSystem: cloth seed submitted for entity {}", (uint32_t)e);
            } else {
                HN_CORE_WARN("ClothSystem: cloth seed submit FAILED for entity {}", (uint32_t)e);
            }
        }

        return any_submitted;
    }

    bool ClothSystem::execute_cloth_sim(FrameGraphPassContext& ctx) {
        bool any_submitted = false;

        for (auto& [e, handle] : m_impl->handles) {
            if (!handle.sim || !handle.sim->valid())
                continue;

            const auto state_a = ctx.get_buffer("clothStateA");
            const auto state_b = ctx.get_buffer("clothStateB");

            if (state_a && state_b) {
                auto vk_a = std::dynamic_pointer_cast<VulkanStorageBuffer>(state_a);
                auto vk_b = std::dynamic_pointer_cast<VulkanStorageBuffer>(state_b);

                if (vk_a && vk_b) {
                    handle.sim->set_external_state_buffers(
                        reinterpret_cast<VkBuffer>(vk_a->get_native_buffer()),
                        reinterpret_cast<VkBuffer>(vk_b->get_native_buffer()));
                } else {
                    handle.sim->set_external_state_buffers(VK_NULL_HANDLE, VK_NULL_HANDLE);
                }
            } else {
                handle.sim->set_external_state_buffers(VK_NULL_HANDLE, VK_NULL_HANDLE);
            }

            const float dt        = m_impl->current_dt;
            const uint32_t fi     = ctx.frame_index();

            const bool submitted = ctx.submit_vulkan_compute([&](VkCommandBuffer cmd) {
                handle.sim->record_sim(cmd, dt, fi);
            });

            if (submitted) {
                handle.sim->swap_ping_pong();
                any_submitted = true;
            }
        }

        return any_submitted;
    }

    void ClothSystem::register_frame_graph_executors() {
        static bool s_registered = false;
        if (s_registered)
            return;

        auto& registry = FrameGraphRegistry::get();

        registry.register_executor("cloth.seed", [](FrameGraphPassContext& ctx) {
            auto* scene = Scene::get_active_scene();
            if (!scene)
                return;
            auto* sys = scene->get_cloth_system();
            if (!sys)
                return;
            sys->execute_cloth_seed(ctx);
        });

        registry.register_executor("cloth.simulate", [](FrameGraphPassContext& ctx) {
            auto* scene = Scene::get_active_scene();
            if (!scene)
                return;
            auto* sys = scene->get_cloth_system();
            if (!sys)
                return;
            sys->execute_cloth_sim(ctx);
        });

        s_registered = true;
    }

} // namespace Honey