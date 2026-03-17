#include "hnpch.h"
#include "frame_graph.h"

#include "frame_graph_registry.h"
#include "renderer.h"
#include "Honey/core/engine.h"

#include <algorithm>
#include <cmath>

namespace Honey {
    namespace {
        static bool try_resolve_dimensions(const FGTextureDesc& desc,
                                           uint32_t& out_width,
                                           uint32_t& out_height)
        {
            switch (desc.size_mode) {
                case FGSizeMode::Fixed:
                    out_width = desc.width;
                    out_height = desc.height;
                    return out_width > 0 && out_height > 0;
                case FGSizeMode::SwapchainRelative: {
                    auto& window = Application::get().get_window();
                    const uint32_t base_w = window.get_width();
                    const uint32_t base_h = window.get_height();
                    if (base_w == 0 || base_h == 0)
                        return false;

                    const float sx = (desc.scale_x > 0.0f) ? desc.scale_x : 1.0f;
                    const float sy = (desc.scale_y > 0.0f) ? desc.scale_y : 1.0f;

                    out_width = std::max(1u, static_cast<uint32_t>(std::round(static_cast<float>(base_w) * sx)));
                    out_height = std::max(1u, static_cast<uint32_t>(std::round(static_cast<float>(base_h) * sy)));
                    return true;
                }
            }

            return false;
        }
    }

    bool FGCompileDiagnostics::has_errors() const {
        return std::ranges::any_of(entries, [](const FGDiagnostic& d) {
            return d.severity == FGDiagSeverity::Error;
        });
    }

    void FGCompileDiagnostics::add_error(std::string_view message, std::string_view scope) {
        FGDiagnostic d{};
        d.severity = FGDiagSeverity::Error;
        d.message = std::string(message);
        d.scope = std::string(scope);
        entries.emplace_back(std::move(d));
    }

    void FGCompileDiagnostics::add_warning(std::string_view message, std::string_view scope) {
        FGDiagnostic d{};
        d.severity = FGDiagSeverity::Warning;
        d.message = std::string(message);
        d.scope = std::string(scope);
        entries.emplace_back(std::move(d));
    }

    void FGCompileDiagnostics::add_info(std::string_view message, std::string_view scope) {
        FGDiagnostic d{};
        d.severity = FGDiagSeverity::Info;
        d.message = std::string(message);
        d.scope = std::string(scope);
        entries.emplace_back(std::move(d));
    }

    bool FrameGraphCompiled::valid() const {
        if (m_passes.empty() || m_resources.empty())
            return false;

        for (const auto& pass : m_passes) {
            if (!pass.executor)
                return false;
        }

        return true;
    }

    void FrameGraphCompiled::execute(const FGExecutionContext& execution_context) {
        if (!valid()) {
            HN_CORE_WARN("FrameGraphCompiled::execute called with invalid graph");
            return;
        }

        for (auto& pass : m_passes) {
            if (pass.targets_swapchain) {
                Renderer::set_render_target(nullptr);
            } else {
                if (!pass.target_framebuffer) {
                    HN_CORE_WARN("FrameGraphCompiled::execute skipping pass '{0}' - no target framebuffer is assigned yet",
                                 pass.name);
                    continue;
                }
                Renderer::set_render_target(pass.target_framebuffer);
            }

            Renderer::begin_pass();

            FrameGraphPassContext ctx(*this, pass, execution_context);
            pass.executor(ctx);

            Renderer::end_pass();
        }
    }

    FGResourceHandle FrameGraphCompiled::find_resource_handle(const std::string& name) const {
        const auto it = m_resource_name_to_handle.find(name);
        if (it == m_resource_name_to_handle.end())
            return k_invalid_resource;
        return it->second;
    }

    FrameGraphPassContext::
    FrameGraphPassContext(FrameGraphCompiled& graph, FGCompiledPass& pass, const FGExecutionContext& execution_context) {
        m_graph = &graph;
        m_pass = &pass;
        m_frame_index = execution_context.frame_index;
        m_exec_context = &execution_context;
    }

    const std::string& FrameGraphPassContext::pass_name() const {
        HN_CORE_ASSERT(m_pass, "FrameGraphPassContext::pass_name: null pass");
        return m_pass->name;
    }

    uint32_t FrameGraphPassContext::frame_index() const {
        return m_frame_index;
    }

    const YAML::Node& FrameGraphPassContext::params() const {
        HN_CORE_ASSERT(m_pass, "FrameGraphPassContext::params: null pass");
        return m_pass->params_node;
    }

    const YAML::Node& FrameGraphPassContext::clear() const {
        HN_CORE_ASSERT(m_pass, "FrameGraphPassContext::clear: null pass");
        return m_pass->clear_node;
    }

    Ref<Framebuffer> FrameGraphPassContext::get_input_framebuffer(const std::string& resource_name) const {
        HN_CORE_ASSERT(m_graph, "FrameGraphPassContext::get_input_framebuffer: null graph");
        HN_CORE_ASSERT(m_pass, "FrameGraphPassContext::get_input_framebuffer: null pass");

        const FGResourceHandle h = m_graph->find_resource_handle(resource_name);
        if (h == k_invalid_resource || h >= m_graph->m_resources.size())
            return nullptr;

        if (std::find(m_pass->reads.begin(), m_pass->reads.end(), h) == m_pass->reads.end()) {
            HN_CORE_WARN("FrameGraphPassContext::get_input_framebuffer: resource '{0}' is not listed as input for pass '{1}'",
                         resource_name, m_pass->name);
        }

        return m_graph->m_resources[h].framebuffer;
    }

    void* FrameGraphPassContext::user_context() const {
        return m_exec_context ? m_exec_context->user_context : nullptr;
    }

    std::shared_ptr<FrameGraphCompiled> FrameGraphCompiler::compile(const FGGraphDesc& desc,
        FGCompileDiagnostics& out_diagnostics,
        const FGCompileOptions* options) {
        auto compiled = std::make_shared<FrameGraphCompiled>();

        if (desc.resources.empty()) {
            out_diagnostics.add_error("Frame graph has no resources");
            return nullptr;
        }

        if (desc.passes.empty()) {
            out_diagnostics.add_error("Frame graph has no passes");
            return nullptr;
        }

        compiled->m_resources.reserve(desc.resources.size());
        for (const auto& res_desc : desc.resources) {
            if (res_desc.name.empty()) {
                out_diagnostics.add_error("Encountered resource with empty name");
                continue;
            }

            if (compiled->m_resource_name_to_handle.find(res_desc.name) != compiled->m_resource_name_to_handle.end()) {
                out_diagnostics.add_error("Duplicate resource name", res_desc.name);
                continue;
            }

            FGCompiledResource res{};
            res.name = res_desc.name;
            res.type = res_desc.type;
            res.texture = res_desc.texture;
            res.imported_kind = res_desc.imported_kind;

            if (res.type == FGResourceType::Texture) {
                uint32_t rw = 0, rh = 0;
                if (!try_resolve_dimensions(res.texture, rw, rh)) {
                    out_diagnostics.add_error("Failed to resolve texture dimensions for resource", res.name);
                } else {
                    res.resolved_width = rw;
                    res.resolved_height = rh;
                }
            } else if (res.type == FGResourceType::ImportedTarget) {
                if (res.imported_kind == FGImportedTargetKind::ExternalFramebuffer) {
                    if (!options) {
                        out_diagnostics.add_error("External framebuffer resource requires compile options binding map", res.name);
                    } else {
                        const auto it = options->external_framebuffers.find(res.name);
                        if (it == options->external_framebuffers.end() || !it->second) {
                            out_diagnostics.add_error("No external framebuffer binding provided for imported target", res.name);
                        } else {
                            res.framebuffer = it->second;

                            const auto& spec = it->second->get_specification();
                            res.resolved_width = spec.width;
                            res.resolved_height = spec.height;
                        }
                    }
                }
            }

            const auto handle = static_cast<FGResourceHandle>(compiled->m_resources.size());
            compiled->m_resource_name_to_handle.emplace(res.name, handle);
            compiled->m_resources.emplace_back(std::move(res));
        }

        compiled->m_passes.reserve(desc.passes.size());
        for (const auto& pass_desc : desc.passes) {
            FGCompiledPass pass{};
            pass.name = pass_desc.name;
            pass.executor_id = pass_desc.executor_id;
            pass.clear_node = pass_desc.clear_node;
            pass.params_node = pass_desc.params_node;

            if (pass_desc.name.empty()) {
                out_diagnostics.add_error("Encountered pass with empty name");
            }

            if (pass_desc.executor_id.empty()) {
                out_diagnostics.add_error("Pass has empty executor_id", pass_desc.name);
            } else {
                pass.executor = FrameGraphRegistry::get().find_executor(pass_desc.executor_id);
                if (!pass.executor) {
                    out_diagnostics.add_error("No executor is registered for pass executor_id", pass_desc.executor_id);
                }
            }

            for (const auto& input_name : pass_desc.reads) {
                const FGResourceHandle h = compiled->find_resource_handle(input_name);
                if (h == k_invalid_resource) {
                    out_diagnostics.add_error("Pass input resource not found", input_name);
                    continue;
                }
                pass.reads.push_back(h);
            }

            for (const auto& output_name : pass_desc.writes) {
                const FGResourceHandle h = compiled->find_resource_handle(output_name);
                if (h == k_invalid_resource) {
                    out_diagnostics.add_error("Pass output resource not found", output_name);
                    continue;
                }
                pass.writes.push_back(h);
            }

            // Minimal v1 target mapping:
            // - If any written resource is ImportedTarget(Swapchain), treat as swapchain pass.
            pass.targets_swapchain = false;
            for (const auto h : pass.writes) {
                if (h >= compiled->m_resources.size())
                    continue;
                const auto& out_res = compiled->m_resources[h];
                if (out_res.type == FGResourceType::ImportedTarget &&
                    out_res.imported_kind == FGImportedTargetKind::Swapchain)
                {
                    pass.targets_swapchain = true;
                    break;
                }
            }

            if (!pass.targets_swapchain) {
                // Prefer imported external framebuffer outputs when present.
                for (const auto h : pass.writes) {
                    if (h >= compiled->m_resources.size())
                        continue;

                    auto& out_res = compiled->m_resources[h];
                    if (out_res.type == FGResourceType::ImportedTarget &&
                        out_res.imported_kind == FGImportedTargetKind::ExternalFramebuffer)
                    {
                        pass.target_framebuffer = out_res.framebuffer;
                        break;
                    }
                }

                // Otherwise allocate framebuffer from written texture resources.
                if (!pass.target_framebuffer) {
                    std::vector<FramebufferTextureSpecification> attachments;
                    uint32_t width = 0;
                    uint32_t height = 0;
                    uint32_t samples = 1;

                    for (const auto h : pass.writes) {
                        if (h >= compiled->m_resources.size())
                            continue;

                        auto& out_res = compiled->m_resources[h];
                        if (out_res.type != FGResourceType::Texture)
                            continue;

                        attachments.emplace_back(out_res.texture.format);

                        if (width == 0 && height == 0) {
                            width = out_res.resolved_width;
                            height = out_res.resolved_height;
                            samples = out_res.texture.samples;
                        } else {
                            if (out_res.resolved_width != width || out_res.resolved_height != height) {
                                out_diagnostics.add_error("Pass writes textures with mismatched resolved sizes", pass.name);
                            }
                            if (out_res.texture.samples != samples) {
                                out_diagnostics.add_error("Pass writes textures with mismatched sample counts", pass.name);
                            }
                        }
                    }

                    if (!attachments.empty()) {
                        if (width == 0 || height == 0) {
                            out_diagnostics.add_error("Cannot allocate pass framebuffer with zero dimensions", pass.name);
                        } else {
                            FramebufferSpecification fb_spec{};
                            fb_spec.width = width;
                            fb_spec.height = height;
                            fb_spec.samples = samples;
                            fb_spec.attachments.attachments = attachments;
                            fb_spec.swap_chain_target = false;

                            pass.target_framebuffer = Framebuffer::create(fb_spec);
                            if (!pass.target_framebuffer) {
                                out_diagnostics.add_error("Failed to allocate pass target framebuffer", pass.name);
                            } else {
                                // Bind written texture resources to this pass framebuffer handle.
                                for (const auto h : pass.writes) {
                                    if (h >= compiled->m_resources.size())
                                        continue;
                                    auto& out_res = compiled->m_resources[h];
                                    if (out_res.type == FGResourceType::Texture) {
                                        out_res.framebuffer = pass.target_framebuffer;
                                    }
                                }
                            }
                        }
                    }
                }

                if (!pass.target_framebuffer) {
                    out_diagnostics.add_warning(
                        "Pass does not target swapchain and no framebuffer target was resolved",
                        pass.name);
                }
            }

            compiled->m_passes.emplace_back(std::move(pass));
        }

        if (out_diagnostics.has_errors())
            return nullptr;

        return compiled;
    }
}
