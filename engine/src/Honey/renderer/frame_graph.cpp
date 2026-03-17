#include "hnpch.h"
#include "frame_graph.h"

#include "frame_graph_registry.h"
#include "renderer.h"
#include "Honey/core/engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <glm/glm.hpp>
#include <queue>
#include <sstream>

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

        static bool try_parse_clear_color(const YAML::Node& clear_node, glm::vec4& out_color) {
            if (!clear_node || !clear_node.IsMap())
                return false;

            auto parse_rgba = [&](const YAML::Node& n) -> bool {
                if (!n || !n.IsSequence() || n.size() != 4)
                    return false;

                try {
                    out_color = {
                        n[0].as<float>(),
                        n[1].as<float>(),
                        n[2].as<float>(),
                        n[3].as<float>()
                    };
                    return true;
                } catch (const YAML::BadConversion&) {
                    return false;
                }
            };

            // Preferred explicit key.
            if (parse_rgba(clear_node["color"]))
                return true;

            // Fallback: first RGBA-looking sequence under Clear map (e.g., sceneColor: [r,g,b,a]).
            for (const auto& kv : clear_node) {
                if (parse_rgba(kv.second))
                    return true;
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

        const bool collect_timings = execution_context.collect_cpu_timings || execution_context.out_stats;
        FGExecutionStats* stats_out = execution_context.out_stats;

        if (stats_out) {
            stats_out->total_cpu_time_ms = 0.0;
            stats_out->pass_stats.clear();
            stats_out->pass_stats.reserve(m_passes.size());
        }

        const auto total_begin = std::chrono::high_resolution_clock::now();

        for (auto& pass : m_passes) {
            FGPassExecutionStat pass_stat{};
            pass_stat.pass_name = pass.name.empty() ? std::string("<unnamed>") : pass.name;

            glm::vec4 clear_color{};
            const bool has_clear_color = try_parse_clear_color(pass.clear_node, clear_color);
            if (has_clear_color) {
                RenderCommand::set_clear_color(clear_color);
            }

            const auto pass_begin = std::chrono::high_resolution_clock::now();

            if (pass.targets_swapchain) {
                Renderer::set_render_target(nullptr);
            } else {
                if (!pass.target_framebuffer) {
                    HN_CORE_WARN("FrameGraphCompiled::execute skipping pass '{0}' - no target framebuffer is assigned yet",
                                 pass.name);

                    pass_stat.skipped = true;
                    if (stats_out)
                        stats_out->pass_stats.emplace_back(std::move(pass_stat));
                    continue;
                }
                Renderer::set_render_target(pass.target_framebuffer);
            }

            Renderer::begin_pass();

            // For OpenGL this performs the actual attachment clear.
            // For Vulkan clear values are consumed by Begin*Pass commands,
            // and RenderCommand::clear() is currently a no-op.
            if (has_clear_color) {
                RenderCommand::clear();
            }

            FrameGraphPassContext ctx(*this, pass, execution_context);
            pass.executor(ctx);

            Renderer::end_pass();

            if (collect_timings) {
                const auto pass_end = std::chrono::high_resolution_clock::now();
                pass_stat.cpu_time_ms =
                    std::chrono::duration<double, std::milli>(pass_end - pass_begin).count();
            }

            if (execution_context.log_pass_execution) {
                HN_CORE_INFO("[FrameGraph] Pass '{0}' executed in {1:.3f} ms",
                             pass_stat.pass_name,
                             pass_stat.cpu_time_ms);
            }

            if (stats_out)
                stats_out->pass_stats.emplace_back(std::move(pass_stat));
        }

        if (collect_timings) {
            const auto total_end = std::chrono::high_resolution_clock::now();
            const double total_ms =
                std::chrono::duration<double, std::milli>(total_end - total_begin).count();

            if (stats_out)
                stats_out->total_cpu_time_ms = total_ms;

            if (execution_context.log_pass_execution) {
                HN_CORE_INFO("[FrameGraph] Total execution CPU time: {0:.3f} ms", total_ms);
            }
        }
    }

    FGResourceHandle FrameGraphCompiled::find_resource_handle(const std::string& name) const {
        const auto it = m_resource_name_to_handle.find(name);
        if (it == m_resource_name_to_handle.end())
            return k_invalid_resource;
        return it->second;
    }

    std::string FrameGraphCompiled::debug_dump() const {
        std::ostringstream oss;

        oss << "FrameGraphCompiled Dump\n";
        oss << "Framebuffer allocations: logical=" << m_logical_framebuffer_allocations
            << " physical=" << m_physical_framebuffer_allocations;
        if (m_logical_framebuffer_allocations > 0) {
            const double savings =
                100.0 * (1.0 - (static_cast<double>(m_physical_framebuffer_allocations) /
                                 static_cast<double>(m_logical_framebuffer_allocations)));
            oss << " savings=" << savings << "%";
        }
        oss << "\n";

        oss << "Resources (" << m_resources.size() << "):\n";
        for (FGResourceHandle h = 0; h < m_resources.size(); ++h) {
            const auto& r = m_resources[h];
            oss << "  [" << h << "] " << r.name << " | ";

            if (r.type == FGResourceType::Texture) {
                oss << "Texture " << r.resolved_width << "x" << r.resolved_height
                    << " samples=" << r.texture.samples;
            } else {
                oss << "ImportedTarget kind="
                    << (r.imported_kind == FGImportedTargetKind::Swapchain ? "Swapchain" : "ExternalFramebuffer");
            }

            oss << " firstUse=";
            if (r.first_use == k_invalid_pass) oss << "-"; else oss << r.first_use;
            oss << " lastUse=";
            if (r.last_use == k_invalid_pass) oss << "-"; else oss << r.last_use;
            if (r.physical_allocation == k_invalid_physical) {
                oss << " phys=-";
            } else {
                oss << " phys=" << r.physical_allocation;
            }
            oss << " hasFB=" << (r.framebuffer ? "yes" : "no");
            oss << "\n";
        }

        oss << "Passes (" << m_passes.size() << "):\n";
        for (FGPassHandle i = 0; i < m_passes.size(); ++i) {
            const auto& p = m_passes[i];
            oss << "  [" << i << "] " << (p.name.empty() ? "<unnamed>" : p.name)
                << " exec='" << p.executor_id << "'"
                << " target=" << (p.targets_swapchain ? "Swapchain" : (p.target_framebuffer ? "Framebuffer" : "None"));

            if (p.physical_allocation != k_invalid_physical) {
                oss << " phys=" << p.physical_allocation;
            }

            oss
                << "\n";

            oss << "      Reads: ";
            if (p.reads.empty()) {
                oss << "(none)";
            } else {
                for (size_t j = 0; j < p.reads.size(); ++j) {
                    const auto h = p.reads[j];
                    if (h < m_resources.size())
                        oss << m_resources[h].name;
                    else
                        oss << "<invalid:" << h << ">";
                    if (j + 1 < p.reads.size()) oss << ", ";
                }
            }
            oss << "\n";

            oss << "      Writes: ";
            if (p.writes.empty()) {
                oss << "(none)";
            } else {
                for (size_t j = 0; j < p.writes.size(); ++j) {
                    const auto h = p.writes[j];
                    if (h < m_resources.size())
                        oss << m_resources[h].name;
                    else
                        oss << "<invalid:" << h << ">";
                    if (j + 1 < p.writes.size()) oss << ", ";
                }
            }
            oss << "\n";
        }

        return oss.str();
    }

    void FrameGraphCompiled::log_debug_dump(std::string_view context_label) const {
        HN_CORE_INFO("[{0}]\n{1}", context_label, debug_dump());
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

                // Texture targets are resolved later by transient physical allocation planning.
                if (!pass.target_framebuffer) {
                    bool has_texture_output = false;
                    for (const auto h : pass.writes) {
                        if (h < compiled->m_resources.size() &&
                            compiled->m_resources[h].type == FGResourceType::Texture)
                        {
                            has_texture_output = true;
                            break;
                        }
                    }

                    if (!has_texture_output) {
                        out_diagnostics.add_warning(
                            "Pass does not target swapchain and no framebuffer target was resolved",
                            pass.name);
                    }
                }
            }

            compiled->m_passes.emplace_back(std::move(pass));
        }

        // Build dataflow dependencies and validate graph-level invariants.
        {
            const uint32_t pass_count = static_cast<uint32_t>(compiled->m_passes.size());
            const uint32_t resource_count = static_cast<uint32_t>(compiled->m_resources.size());

            std::vector<FGPassHandle> producers(resource_count, k_invalid_pass);
            std::vector<std::vector<FGPassHandle>> adjacency(pass_count);
            std::vector<uint32_t> indegree(pass_count, 0);

            auto add_edge = [&](const FGPassHandle from, const FGPassHandle to) {
                if (from == k_invalid_pass || to == k_invalid_pass || from == to)
                    return;

                auto& out_edges = adjacency[from];
                if (std::find(out_edges.begin(), out_edges.end(), to) == out_edges.end()) {
                    out_edges.push_back(to);
                    indegree[to]++;
                }
            };

            // One producer per resource (v1 policy).
            for (FGPassHandle pass_idx = 0; pass_idx < pass_count; ++pass_idx) {
                const auto& pass = compiled->m_passes[pass_idx];
                const std::string pass_label = pass.name.empty() ? std::string("<unnamed>") : pass.name;

                for (const auto h : pass.writes) {
                    if (h >= resource_count)
                        continue;

                    const auto& res = compiled->m_resources[h];

                    if (producers[h] != k_invalid_pass) {
                        const auto& prev = compiled->m_passes[producers[h]];
                        const std::string prev_label = prev.name.empty() ? std::string("<unnamed>") : prev.name;

                        out_diagnostics.add_error(
                            "Resource has multiple writer passes: '" + res.name +
                            "' written by both '" + prev_label + "' and '" + pass_label + "'",
                            pass_label);
                        continue;
                    }

                    producers[h] = pass_idx;
                }
            }

            // Read dependencies + read-before-write checks.
            for (FGPassHandle pass_idx = 0; pass_idx < pass_count; ++pass_idx) {
                const auto& pass = compiled->m_passes[pass_idx];
                const std::string pass_label = pass.name.empty() ? std::string("<unnamed>") : pass.name;

                for (const auto h : pass.reads) {
                    if (h >= resource_count)
                        continue;

                    const auto& res = compiled->m_resources[h];
                    const FGPassHandle producer = producers[h];

                    if (producer == k_invalid_pass) {
                        if (res.type == FGResourceType::Texture) {
                            out_diagnostics.add_error(
                                "Texture resource is read before any pass writes it: '" + res.name + "'",
                                pass_label);
                        }
                        // Imported resources may be externally produced (swapchain/external framebuffer),
                        // so no hard error for v1.
                        continue;
                    }

                    add_edge(producer, pass_idx);
                }
            }

            // Topological sort.
            std::queue<FGPassHandle> zero_indegree;
            for (FGPassHandle i = 0; i < pass_count; ++i) {
                if (indegree[i] == 0)
                    zero_indegree.push(i);
            }

            std::vector<FGPassHandle> topo_order;
            topo_order.reserve(pass_count);

            while (!zero_indegree.empty()) {
                const FGPassHandle u = zero_indegree.front();
                zero_indegree.pop();

                topo_order.push_back(u);

                for (const FGPassHandle v : adjacency[u]) {
                    HN_CORE_ASSERT(v < indegree.size(), "FrameGraph topo: edge target out of range");
                    if (--indegree[v] == 0)
                        zero_indegree.push(v);
                }
            }

            if (topo_order.size() != pass_count) {
                std::string cycle_nodes;
                for (FGPassHandle i = 0; i < pass_count; ++i) {
                    if (indegree[i] == 0)
                        continue;

                    if (!cycle_nodes.empty())
                        cycle_nodes += ", ";
                    const auto& p = compiled->m_passes[i];
                    cycle_nodes += p.name.empty() ? std::string("<unnamed>") : p.name;
                }

                out_diagnostics.add_error(
                    "Frame graph contains a cyclic dependency between passes" +
                    (cycle_nodes.empty() ? std::string() : std::string(": ") + cycle_nodes));
            } else {
                // Reorder passes to dependency-respecting order.
                std::vector<FGCompiledPass> sorted_passes;
                sorted_passes.reserve(pass_count);
                for (const FGPassHandle i : topo_order) {
                    sorted_passes.emplace_back(std::move(compiled->m_passes[i]));
                }
                compiled->m_passes = std::move(sorted_passes);

                // Dead-pass culling by requested outputs.
                {
                    std::vector<bool> needed_resources(resource_count, false);

                    const bool has_explicit_outputs = options && !options->requested_output_resources.empty();
                    if (has_explicit_outputs) {
                        for (const auto& output_name : options->requested_output_resources) {
                            const FGResourceHandle h = compiled->find_resource_handle(output_name);
                            if (h == k_invalid_resource || h >= resource_count) {
                                out_diagnostics.add_error(
                                    "Requested output resource not found: '" + output_name + "'");
                                continue;
                            }
                            needed_resources[h] = true;
                        }
                    } else {
                        // Default outputs = imported targets.
                        for (FGResourceHandle h = 0; h < resource_count; ++h) {
                            const auto& res = compiled->m_resources[h];
                            if (res.type == FGResourceType::ImportedTarget)
                                needed_resources[h] = true;
                        }
                    }

                    const bool any_needed = std::ranges::any_of(needed_resources, [](const bool b) { return b; });
                    if (!any_needed) {
                        out_diagnostics.add_warning("No requested outputs resolved; skipping dead-pass culling");
                    } else {
                        std::vector<bool> keep_pass(compiled->m_passes.size(), false);

                        for (int32_t i = static_cast<int32_t>(compiled->m_passes.size()) - 1; i >= 0; --i) {
                            const auto& pass = compiled->m_passes[static_cast<size_t>(i)];

                            bool writes_needed = false;
                            for (const auto h : pass.writes) {
                                if (h < resource_count && needed_resources[h]) {
                                    writes_needed = true;
                                    break;
                                }
                            }

                            if (!writes_needed)
                                continue;

                            keep_pass[static_cast<size_t>(i)] = true;
                            for (const auto h : pass.reads) {
                                if (h < resource_count)
                                    needed_resources[h] = true;
                            }
                        }

                        std::vector<FGCompiledPass> culled_passes;
                        culled_passes.reserve(compiled->m_passes.size());
                        for (size_t i = 0; i < compiled->m_passes.size(); ++i) {
                            if (!keep_pass[i]) {
                                const auto& p = compiled->m_passes[i];
                                out_diagnostics.add_info(
                                    "Culled dead pass: '" + (p.name.empty() ? std::string("<unnamed>") : p.name) + "'");
                                continue;
                            }
                            culled_passes.emplace_back(std::move(compiled->m_passes[i]));
                        }
                        compiled->m_passes = std::move(culled_passes);
                    }
                }

                // Update first/last use tracking in execution order.
                for (auto& res : compiled->m_resources) {
                    res.first_use = k_invalid_pass;
                    res.last_use = k_invalid_pass;
                }

                for (FGPassHandle pass_idx = 0; pass_idx < compiled->m_passes.size(); ++pass_idx) {
                    const auto& pass = compiled->m_passes[pass_idx];

                    auto mark_use = [&](const FGResourceHandle h) {
                        if (h >= compiled->m_resources.size())
                            return;
                        auto& res = compiled->m_resources[h];
                        if (res.first_use == k_invalid_pass)
                            res.first_use = pass_idx;
                        res.last_use = pass_idx;
                    };

                    for (const auto h : pass.reads)
                        mark_use(h);
                    for (const auto h : pass.writes)
                        mark_use(h);
                }

                auto estimate_format_bytes_per_pixel = [](const FramebufferTextureFormat fmt) -> uint32_t {
                    switch (fmt) {
                        case FramebufferTextureFormat::RGBA8:
                            return 4;
                        case FramebufferTextureFormat::RED_INTEGER:
                            return 4;
                        case FramebufferTextureFormat::DEPTH24STENCIL8:
                            return 4;
                        case FramebufferTextureFormat::None:
                        default:
                            return 4;
                    }
                };

                struct TransientPassTargetCandidate {
                    FGPassHandle pass_index = k_invalid_pass;
                    FGPassHandle first_use = k_invalid_pass;
                    FGPassHandle last_use = k_invalid_pass;

                    uint32_t width = 0;
                    uint32_t height = 0;
                    uint32_t samples = 1;

                    std::vector<FramebufferTextureSpecification> attachments;
                    std::vector<FGResourceHandle> resources;

                    uint64_t approx_bytes = 0;
                };

                struct PhysicalAllocation {
                    uint32_t width = 0;
                    uint32_t height = 0;
                    uint32_t samples = 1;
                    std::vector<FramebufferTextureSpecification> attachments;
                    Ref<Framebuffer> framebuffer;
                    std::vector<size_t> assigned_candidate_indices;

                    bool compatible_with(const TransientPassTargetCandidate& c) const {
                        if (c.width != width || c.height != height)
                            return false;
                        if (c.samples != samples)
                            return false;
                        if (c.attachments.size() != attachments.size())
                            return false;

                        for (size_t i = 0; i < attachments.size(); ++i) {
                            if (attachments[i].texture_format != c.attachments[i].texture_format)
                                return false;
                        }

                        return true;
                    }
                };

                auto lifetimes_overlap = [&](const FGPassHandle a_first,
                                             const FGPassHandle a_last,
                                             const FGPassHandle b_first,
                                             const FGPassHandle b_last)
                {
                    if (a_first == k_invalid_pass || a_last == k_invalid_pass)
                        return false;
                    if (b_first == k_invalid_pass || b_last == k_invalid_pass)
                        return false;
                    return !(a_last < b_first || b_last < a_first);
                };

                for (auto& r : compiled->m_resources) {
                    if (r.type == FGResourceType::Texture) {
                        r.physical_allocation = k_invalid_physical;
                        r.framebuffer.reset();
                    }
                }

                for (auto& pass : compiled->m_passes) {
                    pass.physical_allocation = k_invalid_physical;
                }

                std::vector<TransientPassTargetCandidate> candidates;
                candidates.reserve(compiled->m_passes.size());

                for (FGPassHandle pass_idx = 0; pass_idx < compiled->m_passes.size(); ++pass_idx) {
                    auto& pass = compiled->m_passes[pass_idx];

                    if (pass.targets_swapchain)
                        continue;
                    if (pass.target_framebuffer)
                        continue; // imported external target already resolved.

                    TransientPassTargetCandidate candidate{};
                    candidate.pass_index = pass_idx;

                    bool have_dimensions = false;
                    uint32_t bytes_per_pixel_sum = 0;

                    for (const auto h : pass.writes) {
                        if (h >= compiled->m_resources.size())
                            continue;

                        const auto& r = compiled->m_resources[h];
                        if (r.type != FGResourceType::Texture)
                            continue;

                        candidate.resources.push_back(h);
                        candidate.attachments.emplace_back(r.texture.format);
                        bytes_per_pixel_sum += estimate_format_bytes_per_pixel(r.texture.format);

                        if (!have_dimensions) {
                            candidate.width = r.resolved_width;
                            candidate.height = r.resolved_height;
                            candidate.samples = r.texture.samples;
                            have_dimensions = true;
                        } else {
                            if (r.resolved_width != candidate.width || r.resolved_height != candidate.height) {
                                out_diagnostics.add_error(
                                    "Pass writes textures with mismatched resolved sizes",
                                    pass.name);
                            }
                            if (r.texture.samples != candidate.samples) {
                                out_diagnostics.add_error(
                                    "Pass writes textures with mismatched sample counts",
                                    pass.name);
                            }
                        }

                        if (candidate.first_use == k_invalid_pass || r.first_use < candidate.first_use)
                            candidate.first_use = r.first_use;
                        if (candidate.last_use == k_invalid_pass || r.last_use > candidate.last_use)
                            candidate.last_use = r.last_use;
                    }

                    if (candidate.resources.empty()) {
                        out_diagnostics.add_warning(
                            "Pass does not target swapchain and no framebuffer target was resolved",
                            pass.name);
                        continue;
                    }

                    if (candidate.width == 0 || candidate.height == 0) {
                        out_diagnostics.add_error(
                            "Cannot allocate pass framebuffer with zero dimensions",
                            pass.name);
                        continue;
                    }

                    if (candidate.first_use == k_invalid_pass)
                        candidate.first_use = pass_idx;
                    if (candidate.last_use == k_invalid_pass)
                        candidate.last_use = pass_idx;

                    candidate.approx_bytes =
                        static_cast<uint64_t>(candidate.width) *
                        static_cast<uint64_t>(candidate.height) *
                        static_cast<uint64_t>(candidate.samples) *
                        static_cast<uint64_t>(std::max(1u, bytes_per_pixel_sum));

                    candidates.emplace_back(std::move(candidate));
                }

                std::vector<PhysicalAllocation> physical_allocations;
                std::vector<uint32_t> candidate_to_physical(candidates.size(), k_invalid_physical);

                std::vector<size_t> candidate_order(candidates.size());
                for (size_t i = 0; i < candidates.size(); ++i)
                    candidate_order[i] = i;

                std::sort(candidate_order.begin(), candidate_order.end(), [&](const size_t lhs, const size_t rhs) {
                    const auto& a = candidates[lhs];
                    const auto& b = candidates[rhs];

                    if (a.approx_bytes != b.approx_bytes)
                        return a.approx_bytes > b.approx_bytes; // large-first packing
                    if (a.first_use != b.first_use)
                        return a.first_use < b.first_use;
                    return a.pass_index < b.pass_index;
                });

                for (const size_t candidate_index : candidate_order) {
                    const auto& c = candidates[candidate_index];

                    uint32_t chosen = k_invalid_physical;
                    for (uint32_t i = 0; i < physical_allocations.size(); ++i) {
                        const auto& alloc = physical_allocations[i];
                        if (!alloc.compatible_with(c))
                            continue;

                        bool overlaps = false;
                        for (const auto other_candidate_index : alloc.assigned_candidate_indices) {
                            if (other_candidate_index >= candidates.size())
                                continue;

                            const auto& other = candidates[other_candidate_index];
                            if (lifetimes_overlap(c.first_use, c.last_use, other.first_use, other.last_use)) {
                                overlaps = true;
                                break;
                            }
                        }

                        if (!overlaps) {
                            chosen = i;
                            break;
                        }
                    }

                    if (chosen == k_invalid_physical) {
                        PhysicalAllocation alloc{};
                        alloc.width = c.width;
                        alloc.height = c.height;
                        alloc.samples = c.samples;
                        alloc.attachments = c.attachments;
                        physical_allocations.emplace_back(std::move(alloc));
                        chosen = static_cast<uint32_t>(physical_allocations.size() - 1);
                    }

                    candidate_to_physical[candidate_index] = chosen;
                    physical_allocations[chosen].assigned_candidate_indices.push_back(candidate_index);
                }

                for (auto& alloc : physical_allocations) {
                    FramebufferSpecification fb_spec{};
                    fb_spec.width = alloc.width;
                    fb_spec.height = alloc.height;
                    fb_spec.samples = alloc.samples;
                    fb_spec.attachments.attachments = alloc.attachments;
                    fb_spec.swap_chain_target = false;

                    alloc.framebuffer = Framebuffer::create(fb_spec);
                    if (!alloc.framebuffer) {
                        out_diagnostics.add_error("Failed to allocate transient physical framebuffer");
                    }
                }

                for (size_t candidate_index = 0; candidate_index < candidates.size(); ++candidate_index) {
                    const auto physical_index = candidate_to_physical[candidate_index];
                    if (physical_index == k_invalid_physical || physical_index >= physical_allocations.size())
                        continue;

                    const auto& c = candidates[candidate_index];
                    if (c.pass_index >= compiled->m_passes.size())
                        continue;

                    auto& pass = compiled->m_passes[c.pass_index];
                    pass.physical_allocation = physical_index;
                    pass.target_framebuffer = physical_allocations[physical_index].framebuffer;

                    for (const auto h : c.resources) {
                        if (h >= compiled->m_resources.size())
                            continue;
                        auto& r = compiled->m_resources[h];
                        r.physical_allocation = physical_index;
                        r.framebuffer = physical_allocations[physical_index].framebuffer;
                    }
                }

                for (const auto& pass : compiled->m_passes) {
                    if (!pass.targets_swapchain && !pass.target_framebuffer) {
                        out_diagnostics.add_warning(
                            "Pass does not target swapchain and no framebuffer target was resolved",
                            pass.name);
                    }
                }

                compiled->m_physical_framebuffer_allocations = static_cast<uint32_t>(physical_allocations.size());
                compiled->m_logical_framebuffer_allocations = static_cast<uint32_t>(candidates.size());
            }
        }

        if (out_diagnostics.has_errors())
            return nullptr;

        return compiled;
    }
}
