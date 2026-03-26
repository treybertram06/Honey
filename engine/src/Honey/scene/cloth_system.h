#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <memory>

namespace Honey {

    class FrameGraphPassContext;
    struct ClothComponent;

    class ClothSystem {
    public:
        ClothSystem();
        ~ClothSystem();

        // Called from Scene::on_runtime_start — inits GPU handles for all ClothComponent entities.
        void on_start(entt::registry& registry);

        // Called from Scene::on_runtime_stop — destroys all GPU handles.
        void on_stop(entt::registry& registry);

        // Records cloth draw commands into the current render pass.
        // Must be called after Renderer3D::end_scene() inside on_update_runtime.
        void on_render(entt::registry& registry, const glm::mat4& vp);

        // Store the current frame dt so the cloth.simulate executor can read it.
        void set_frame_dt(float dt);

        // FrameGraph executor targets — routed from ClothSystem::register_frame_graph_executors().
        bool execute_cloth_seed(FrameGraphPassContext& ctx);
        bool execute_cloth_sim(FrameGraphPassContext& ctx);

        // Register cloth.seed and cloth.simulate executors into the global FrameGraphRegistry.
        // Idempotent — safe to call multiple times.
        static void register_frame_graph_executors();

    private:
        // Pimpl: hides VulkanClothSim / VulkanClothRenderer from headers that include this file.
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

} // namespace Honey