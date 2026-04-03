#include "hnpch.h"
#include "frame_graph_loader.h"

#include <fstream>
#include <sstream>

#include <yaml-cpp/yaml.h>

namespace Honey {
    namespace {

        static std::string to_lower_copy(const std::string& s) {
            std::string out = s;
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return out;
        }

        static bool iequals(const std::string& a, const std::string& b) {
            return to_lower_copy(a) == to_lower_copy(b);
        }

        static bool parse_resource_type(const YAML::Node& n,
                                        FGResourceType& out,
                                        FGCompileDiagnostics& diags,
                                        std::string_view scope)
        {
            if (!n || !n.IsScalar()) {
                diags.add_error("Resource Type must be a scalar string", scope);
                return false;
            }

            const std::string v = n.as<std::string>();
            if (iequals(v, "texture")) {
                out = FGResourceType::Texture;
                return true;
            }
            if (iequals(v, "buffer")) {
                out = FGResourceType::Buffer;
                return true;
            }
            if (iequals(v, "importedtarget") || iequals(v, "imported_target")) {
                out = FGResourceType::ImportedTarget;
                return true;
            }

            diags.add_error("Unknown resource Type: " + v, scope);
            return false;
        }

        static bool parse_queue_domain(const YAML::Node& n,
                                       FGQueueDomain& out,
                                       FGCompileDiagnostics& diags,
                                       std::string_view scope)
        {
            if (!n || !n.IsScalar()) {
                diags.add_error("Queue must be a scalar string", scope);
                return false;
            }

            const std::string v = n.as<std::string>();
            if (iequals(v, "graphics")) {
                out = FGQueueDomain::Graphics;
                return true;
            }
            if (iequals(v, "compute")) {
                out = FGQueueDomain::Compute;
                return true;
            }
            if (iequals(v, "transfer")) {
                out = FGQueueDomain::Transfer;
                return true;
            }

            diags.add_error("Unknown Queue domain: " + v, scope);
            return false;
        }

        static bool parse_resource_usage(const YAML::Node& n,
                                         FGResourceUsage& out,
                                         FGCompileDiagnostics& diags,
                                         std::string_view scope)
        {
            if (!n || !n.IsScalar()) {
                diags.add_error("Usage must be a scalar string", scope);
                return false;
            }

            const std::string v = n.as<std::string>();

            if (iequals(v, "sampled")) { out = FGResourceUsage::Sampled; return true; }
            if (iequals(v, "uniform")) { out = FGResourceUsage::Uniform; return true; }
            if (iequals(v, "indirect")) { out = FGResourceUsage::Indirect; return true; }
            if (iequals(v, "vertexbuffer") || iequals(v, "vertex_buffer")) { out = FGResourceUsage::VertexBuffer; return true; }
            if (iequals(v, "indexbuffer") || iequals(v, "index_buffer")) { out = FGResourceUsage::IndexBuffer; return true; }
            if (iequals(v, "transfersrc") || iequals(v, "transfer_src")) { out = FGResourceUsage::TransferSrc; return true; }

            if (iequals(v, "colorattachment") || iequals(v, "color_attachment")) { out = FGResourceUsage::ColorAttachment; return true; }
            if (iequals(v, "depthattachment") || iequals(v, "depth_attachment")) { out = FGResourceUsage::DepthAttachment; return true; }
            if (iequals(v, "storagewrite") || iequals(v, "storage_write")) { out = FGResourceUsage::StorageWrite; return true; }
            if (iequals(v, "transferdst") || iequals(v, "transfer_dst")) { out = FGResourceUsage::TransferDst; return true; }

            if (iequals(v, "storageread") || iequals(v, "storage_read")) { out = FGResourceUsage::StorageRead; return true; }
            if (iequals(v, "storagereadwrite") || iequals(v, "storage_read_write") || iequals(v, "storage_rw")) {
                out = FGResourceUsage::StorageReadWrite;
                return true;
            }

            diags.add_error("Unknown Usage: " + v, scope);
            return false;
        }

        static bool parse_binding_sequence(const YAML::Node& node,
                                           std::vector<FGResourceBindingDesc>& out,
                                           std::string_view field_name,
                                           FGCompileDiagnostics& diags,
                                           std::string_view scope)
        {
            if (!node)
                return true;

            if (!node.IsSequence()) {
                diags.add_error(std::string(field_name) + " must be a sequence", scope);
                return false;
            }

            for (const auto& entry : node) {
                FGResourceBindingDesc binding{};

                if (entry.IsScalar()) {
                    binding.resource_name = entry.as<std::string>();
                } else if (entry.IsMap()) {
                    const auto name_node = entry["Name"] ? entry["Name"] : entry["Resource"];
                    if (!name_node || !name_node.IsScalar()) {
                        diags.add_error(std::string(field_name) + " entry is missing Name/Resource", scope);
                        continue;
                    }
                    binding.resource_name = name_node.as<std::string>();

                    if (const auto usage_node = entry["Usage"]) {
                        parse_resource_usage(usage_node, binding.usage, diags, scope);
                    }
                } else {
                    diags.add_error(std::string(field_name) + " contains unsupported entry type", scope);
                    continue;
                }

                if (binding.resource_name.empty()) {
                    diags.add_error(std::string(field_name) + " contains empty resource name", scope);
                    continue;
                }

                out.push_back(std::move(binding));
            }

            return true;
        }

        static bool parse_imported_kind(const YAML::Node& n,
                                        FGImportedTargetKind& out,
                                        FGCompileDiagnostics& diags,
                                        std::string_view scope)
        {
            if (!n || !n.IsScalar()) {
                diags.add_error("ImportedTarget Kind must be a scalar string", scope);
                return false;
            }

            const std::string v = n.as<std::string>();
            if (iequals(v, "swapchain")) {
                out = FGImportedTargetKind::Swapchain;
                return true;
            }
            if (iequals(v, "externalframebuffer") ||
                iequals(v, "external_framebuffer") ||
                iequals(v, "framebuffer")) {
                out = FGImportedTargetKind::ExternalFramebuffer;
                return true;
            }

            diags.add_error("Unknown ImportedTarget Kind: " + v, scope);
            return false;
        }

        static bool parse_texture_format(const YAML::Node& n,
                                         FramebufferTextureFormat& out,
                                         FGCompileDiagnostics& diags,
                                         std::string_view scope)
        {
            if (!n || !n.IsScalar()) {
                diags.add_error("Texture Format must be a scalar string", scope);
                return false;
            }

            const std::string v = n.as<std::string>();
            if (iequals(v, "rgba8")) {
                out = FramebufferTextureFormat::RGBA8;
                return true;
            }
            if (iequals(v, "red_integer") || iequals(v, "redinteger")) {
                out = FramebufferTextureFormat::RED_INTEGER;
                return true;
            }
            if (iequals(v, "depth24stencil8") || iequals(v, "depth") || iequals(v, "depth24_stencil8")) {
                out = FramebufferTextureFormat::DEPTH24STENCIL8;
                return true;
            }

            diags.add_error("Unknown texture Format: " + v, scope);
            return false;
        }

        static bool parse_string_sequence(const YAML::Node& node,
                                          std::vector<std::string>& out,
                                          std::string_view field_name,
                                          FGCompileDiagnostics& diags,
                                          std::string_view scope)
        {
            if (!node)
                return true;

            if (!node.IsSequence()) {
                diags.add_error(std::string(field_name) + " must be a sequence of strings", scope);
                return false;
            }

            for (const auto& v : node) {
                if (!v.IsScalar()) {
                    diags.add_error(std::string(field_name) + " contains a non-scalar entry", scope);
                    continue;
                }
                out.push_back(v.as<std::string>());
            }

            return true;
        }

        static std::string_view diag_severity_label(const FGDiagSeverity severity) {
            switch (severity) {
                case FGDiagSeverity::Info:    return "Info";
                case FGDiagSeverity::Warning: return "Warning";
                case FGDiagSeverity::Error:   return "Error";
            }
            return "Unknown";
        }

        static bool parse_storage_buffer_usage(const YAML::Node& n,
                                       StorageBufferUsage& out,
                                       FGCompileDiagnostics& diags,
                                       std::string_view scope)
        {
            if (!n || !n.IsScalar()) {
                diags.add_error("Buffer Usage must be a scalar string", scope);
                return false;
            }

            const std::string v = n.as<std::string>();

            if (iequals(v, "default")) {
                out = StorageBufferUsage::Default;
                return true;
            }
            if (iequals(v, "dynamic")) {
                out = StorageBufferUsage::Dynamic;
                return true;
            }
            if (iequals(v, "immutable") || iequals(v, "static")) {
                out = StorageBufferUsage::Immutable;
                return true;
            }
            if (iequals(v, "readback") || iequals(v, "read_back")) {
                out = StorageBufferUsage::Readback;
                return true;
            }

            diags.add_error("Unknown Buffer Usage: " + v, scope);
            return false;
        }

    } // namespace

    bool FrameGraphLoader::load_from_file(const std::filesystem::path& file_path,
                                          FGGraphDesc& out_desc,
                                          FGCompileDiagnostics& out_diagnostics)
    {
        out_desc = {};
        out_diagnostics.entries.clear();

        if (!std::filesystem::exists(file_path)) {
            out_diagnostics.add_error("Frame graph file does not exist", file_path.string());
            return false;
        }

        std::ifstream stream(file_path);
        if (!stream.is_open()) {
            out_diagnostics.add_error("Failed to open frame graph file", file_path.string());
            return false;
        }

        std::stringstream ss;
        ss << stream.rdbuf();

        const std::string debug_name = file_path.string();
        return load_from_string(ss.str(), out_desc, out_diagnostics, debug_name);
    }

    bool FrameGraphLoader::load_from_string(std::string_view yaml_text,
                                            FGGraphDesc& out_desc,
                                            FGCompileDiagnostics& out_diagnostics,
                                            std::string_view debug_name)
    {
        out_desc = {};
        out_diagnostics.entries.clear();

        YAML::Node root;
        try {
            root = YAML::Load(std::string(yaml_text));
        } catch (const YAML::ParserException& e) {
            out_diagnostics.add_error(std::string("Failed to parse frame graph YAML: ") + e.what(), debug_name);
            return false;
        }

        if (!root || !root.IsMap()) {
            out_diagnostics.add_error("Frame graph root must be a YAML map", debug_name);
            return false;
        }

        YAML::Node fg = root["FrameGraph"];
        if (!fg || !fg.IsMap()) {
            out_diagnostics.add_error("Missing or invalid 'FrameGraph' root map", debug_name);
            return false;
        }

        if (const auto version = fg["Version"]) {
            if (!version.IsScalar()) {
                out_diagnostics.add_error("FrameGraph.Version must be a scalar integer", debug_name);
            } else {
                try {
                    out_desc.version = version.as<uint32_t>();
                } catch (const YAML::BadConversion&) {
                    out_diagnostics.add_error("FrameGraph.Version must be an integer", debug_name);
                }
            }
        }

        const YAML::Node resources_node = fg["Resources"];
        if (!resources_node || !resources_node.IsMap()) {
            out_diagnostics.add_error("FrameGraph.Resources must be a map", debug_name);
        } else {
            for (const auto& item : resources_node) {
                if (!item.first.IsScalar() || !item.second.IsMap()) {
                    out_diagnostics.add_error("Each resource entry must be 'name: { ... }'", debug_name);
                    continue;
                }

                FGResourceDesc resource{};
                resource.name = item.first.as<std::string>();
                const YAML::Node body = item.second;
                const std::string scope = "Resource:" + resource.name;

                const auto type_node = body["Type"];
                if (type_node) {
                    parse_resource_type(type_node, resource.type, out_diagnostics, scope);
                } else {
                    resource.type = FGResourceType::Texture;
                }

                if (resource.type == FGResourceType::Texture) {
                    parse_texture_format(body["Format"], resource.texture.format, out_diagnostics, scope);

                    const YAML::Node width_node = body["Width"];
                    const YAML::Node height_node = body["Height"];

                    const bool width_swapchain =
                        width_node && width_node.IsScalar() && iequals(width_node.as<std::string>(), "swapchain");
                    const bool height_swapchain =
                        height_node && height_node.IsScalar() && iequals(height_node.as<std::string>(), "swapchain");

                    if (width_swapchain || height_swapchain) {
                        if (!(width_swapchain && height_swapchain)) {
                            out_diagnostics.add_error("Width/Height must both be 'swapchain' when using swapchain-relative size", scope);
                        }

                        resource.texture.size_mode = FGSizeMode::SwapchainRelative;
                        resource.texture.scale_x = 1.0f;
                        resource.texture.scale_y = 1.0f;

                        if (const auto scale_node = body["Scale"]) {
                            try {
                                if (scale_node.IsScalar()) {
                                    const float s = scale_node.as<float>();
                                    resource.texture.scale_x = s;
                                    resource.texture.scale_y = s;
                                } else if (scale_node.IsSequence() && scale_node.size() == 2) {
                                    resource.texture.scale_x = scale_node[0].as<float>();
                                    resource.texture.scale_y = scale_node[1].as<float>();
                                } else {
                                    out_diagnostics.add_error("Scale must be a float or [x, y]", scope);
                                }
                            } catch (const YAML::BadConversion&) {
                                out_diagnostics.add_error("Scale contains non-float values", scope);
                            }
                        }
                    } else if (width_node && height_node) {
                        resource.texture.size_mode = FGSizeMode::Fixed;
                        try {
                            resource.texture.width = width_node.as<uint32_t>();
                            resource.texture.height = height_node.as<uint32_t>();
                            if (resource.texture.width == 0 || resource.texture.height == 0) {
                                out_diagnostics.add_error("Fixed Width/Height must be > 0", scope);
                            }
                        } catch (const YAML::BadConversion&) {
                            out_diagnostics.add_error("Fixed Width/Height must be integers", scope);
                        }
                    } else {
                        // default behavior for incomplete size declarations
                        resource.texture.size_mode = FGSizeMode::SwapchainRelative;
                    }

                    if (const auto samples_node = body["Samples"]) {
                        try {
                            resource.texture.samples = samples_node.as<uint32_t>();
                            if (resource.texture.samples == 0) {
                                out_diagnostics.add_error("Samples must be >= 1", scope);
                                resource.texture.samples = 1;
                            }
                        } catch (const YAML::BadConversion&) {
                            out_diagnostics.add_error("Samples must be an integer", scope);
                        }
                    }
                } else if (resource.type == FGResourceType::Buffer) {
                    const auto size_node = body["Size"];
                    if (!size_node) {
                        out_diagnostics.add_error("Buffer resource requires Size", scope);
                    } else {
                        try {
                            resource.buffer.size = size_node.as<uint64_t>();
                            if (resource.buffer.size == 0) {
                                out_diagnostics.add_error("Buffer Size must be > 0", scope);
                            }
                        } catch (const YAML::BadConversion&) {
                            out_diagnostics.add_error("Buffer Size must be an integer", scope);
                        }
                    }

                    if (const auto usage_node = body["Usage"]) {
                        parse_storage_buffer_usage(usage_node, resource.buffer.usage, out_diagnostics, scope);
                    } else {
                        resource.buffer.usage = StorageBufferUsage::Default;
                    }
                } else if (resource.type == FGResourceType::ImportedTarget) {
                    parse_imported_kind(body["Kind"], resource.imported_kind, out_diagnostics, scope);
                }

                out_desc.resources.emplace_back(std::move(resource));
            }
        }

        const YAML::Node passes_node = fg["Passes"];
        if (!passes_node || !passes_node.IsSequence()) {
            out_diagnostics.add_error("FrameGraph.Passes must be a sequence", debug_name);
        } else {
            for (const auto& pass_node : passes_node) {
                if (!pass_node.IsMap()) {
                    out_diagnostics.add_error("Each pass must be a map", debug_name);
                    continue;
                }

                FGPassDesc pass{};
                if (const auto name = pass_node["Name"]; name && name.IsScalar()) {
                    pass.name = name.as<std::string>();
                }
                const std::string scope = pass.name.empty() ? std::string("Pass:<unnamed>") : "Pass:" + pass.name;

                if (pass.name.empty()) {
                    out_diagnostics.add_error("Pass is missing Name", scope);
                }

                if (const auto exec = pass_node["Executor"]; exec && exec.IsScalar()) {
                    pass.executor_id = exec.as<std::string>();
                } else {
                    out_diagnostics.add_error("Pass is missing Executor", scope);
                }

                if (const auto queue = pass_node["Queue"]) {
                    parse_queue_domain(queue, pass.queue_domain, out_diagnostics, scope);
                }

                parse_string_sequence(pass_node["Reads"], pass.reads, "Reads", out_diagnostics, scope);
                parse_string_sequence(pass_node["Writes"], pass.writes, "Writes", out_diagnostics, scope);

                parse_binding_sequence(pass_node["ReadBindings"], pass.read_bindings, "ReadBindings", out_diagnostics, scope);
                parse_binding_sequence(pass_node["WriteBindings"], pass.write_bindings, "WriteBindings", out_diagnostics, scope);

                // If explicit binding records are present, they are the source of truth.
                if (!pass.read_bindings.empty()) {
                    pass.reads.clear();
                    pass.reads.reserve(pass.read_bindings.size());
                    for (const auto& b : pass.read_bindings)
                        pass.reads.push_back(b.resource_name);
                }

                if (!pass.write_bindings.empty()) {
                    pass.writes.clear();
                    pass.writes.reserve(pass.write_bindings.size());
                    for (const auto& b : pass.write_bindings)
                        pass.writes.push_back(b.resource_name);
                }

                if (pass.writes.empty()) {
                    out_diagnostics.add_error("Pass must define at least one output in Writes", scope);
                }

                if (const auto clear = pass_node["Clear"]) {
                    pass.clear_node = clear;
                }
                if (const auto params = pass_node["Params"]) {
                    pass.params_node = params;
                }

                out_desc.passes.emplace_back(std::move(pass));
            }
        }

        return !out_diagnostics.has_errors();
    }

    std::shared_ptr<FrameGraphCompiled>
    FrameGraphLoader::load_and_compile_from_file(const std::filesystem::path& file_path,
                                                  FGCompileDiagnostics& out_diagnostics,
                                                  const FGCompileOptions* options)
    {
        FGGraphDesc desc{};
        if (!load_from_file(file_path, desc, out_diagnostics)) {
            return nullptr;
        }

        return FrameGraphCompiler::compile(desc, out_diagnostics, options);
    }

    void FrameGraphLoader::log_diagnostics(const FGCompileDiagnostics& diagnostics,
                                           std::string_view context_label)
    {
        size_t error_count = 0;
        size_t warning_count = 0;
        size_t info_count = 0;

        for (const auto& d : diagnostics.entries) {
            switch (d.severity) {
                case FGDiagSeverity::Error:   ++error_count; break;
                case FGDiagSeverity::Warning: ++warning_count; break;
                case FGDiagSeverity::Info:    ++info_count; break;
            }
        }

        HN_CORE_INFO("[{0}] diagnostics: {1} error(s), {2} warning(s), {3} info", context_label, error_count, warning_count, info_count);

        for (const auto& d : diagnostics.entries) {
            std::string msg = std::string("[") + std::string(diag_severity_label(d.severity)) + "] " + d.message;

            if (!d.scope.empty()) {
                msg += " | scope=" + d.scope;
            }
            if (!d.file.empty()) {
                msg += " | file=" + d.file;
            }

            switch (d.severity) {
                case FGDiagSeverity::Error:
                    HN_CORE_ERROR("[{0}] {1}", context_label, msg);
                    break;
                case FGDiagSeverity::Warning:
                    HN_CORE_WARN("[{0}] {1}", context_label, msg);
                    break;
                case FGDiagSeverity::Info:
                    HN_CORE_INFO("[{0}] {1}", context_label, msg);
                    break;
            }
        }
    }

}
