#include "hnpch.h"
#include "shader_compiler.h"
#include "Honey/core/log.h"

#include <fstream>
#include <sstream>
#include <regex>
#include <shaderc/shaderc.hpp>
#include <glad/glad.h>

namespace Honey {

    inline int query_slots_clamped(int required = 16) {
        GLint max_units = 0;
        glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &max_units);
        return std::min(required, static_cast<int>(max_units));
    }

ShaderCompiler::CompilationResult ShaderCompiler::compile_glsl_to_spirv(const std::filesystem::path& shader_path) {
    CompilationResult result;

    try {
        // Read and parse the shader file
        auto shader_sources = parse_shader_file(shader_path);

        if (shader_sources.vertex_source.empty() || shader_sources.fragment_source.empty()) {
            result.error_message = "Failed to parse vertex or fragment shader from file: " + shader_path.string();
            return result;
        }

        // Compile vertex shader
        result.vertex_spirv = compile_single_stage(shader_sources.vertex_source, ShaderStage::Vertex);
        if (result.vertex_spirv.empty()) {
            result.error_message = "Failed to compile vertex shader stage";
            return result;
        }

        // Compile fragment shader
        result.fragment_spirv = compile_single_stage(shader_sources.fragment_source, ShaderStage::Fragment);
        if (result.fragment_spirv.empty()) {
            result.error_message = "Failed to compile fragment shader stage";
            return result;
        }

        // Validate the compiled SPIR-V
        if (!validate_spirv(result.vertex_spirv) || !validate_spirv(result.fragment_spirv)) {
            result.error_message = "Generated SPIR-V failed validation";
            return result;
        }

        result.success = true;
        HN_CORE_INFO("Successfully compiled shader: {0}", shader_path.string());

    } catch (const std::exception& e) {
        result.error_message = "Shader compilation failed: " + std::string(e.what());
        HN_CORE_ERROR("Shader compilation error: {0}", e.what());
    }

    return result;
}

ShaderCompiler::CompilationResult ShaderCompiler::compile_hlsl_to_spirv(const std::filesystem::path& shader_path) {
    CompilationResult result;
    result.success = false;
    result.error_message = "HLSL to SPIR-V compilation not yet implemented";

    HN_CORE_WARN("HLSL compilation requested but not implemented: {0}", shader_path.string());
    return result;
}

std::vector<uint32_t> ShaderCompiler::compile_single_stage(const std::string& source,
                                                           ShaderStage stage,
                                                           const std::string& entry_point) {
    shaderc_shader_kind kind;
    const char* stage_name = "unknown";
    switch (stage) {
        case ShaderStage::Vertex:                 kind = shaderc_glsl_vertex_shader;        stage_name = "vertex"; break;
        case ShaderStage::Fragment:               kind = shaderc_glsl_fragment_shader;      stage_name = "fragment"; break;
        case ShaderStage::Compute:                kind = shaderc_glsl_compute_shader;       stage_name = "compute"; break;
        case ShaderStage::Geometry:               kind = shaderc_glsl_geometry_shader;      stage_name = "geometry"; break;
        case ShaderStage::TessellationControl:    kind = shaderc_glsl_tess_control_shader;  stage_name = "tess_control"; break;
        case ShaderStage::TessellationEvaluation: kind = shaderc_glsl_tess_evaluation_shader; stage_name = "tess_eval"; break;
        default:                                  kind = shaderc_glsl_infer_from_source;    break;
    }

    shaderc::Compiler compiler;
    shaderc::CompileOptions options;

    // Explicitly set source language
    options.SetSourceLanguage(shaderc_source_language_glsl);

    // Target OpenGL environment since we feed SPIR-V to OpenGL via GL_ARB_gl_spirv
    options.SetTargetEnvironment(shaderc_target_env_opengl, shaderc_env_version_opengl_4_5);

    // Generate SPIR-V 1.3 which is compatible with GL 4.5 path
    options.SetTargetSpirv(shaderc_spirv_version_1_3);

    // Debug info in Debug builds, optimize in Release
#ifdef BUILD_DEBUG
    options.SetGenerateDebugInfo();
    options.SetOptimizationLevel(shaderc_optimization_level_zero);
    // Do not suppress warnings in Debug
#else
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    // Suppress warnings in Release
    options.SetSuppressWarnings();
#endif

    // Keep explicit bindings under our control
    options.SetAutoBindUniforms(false);

        const int k_required = 16;
        const int slots_to_use = query_slots_clamped(k_required);
        options.AddMacroDefinition("MAX_TEXTURE_SLOTS", std::to_string(slots_to_use));

    // Provide a basic includer which resolves from assets/shaders for both <> and "" includes
    class FSIncluder : public shaderc::CompileOptions::IncluderInterface {
    public:
        struct IncludeData { std::string content; std::string source_name; };
        shaderc_include_result* GetInclude(const char* requested_source,
                                           shaderc_include_type /*type*/,
                                           const char* /*requesting_source*/,
                                           size_t) override {
            namespace fs = std::filesystem;
            fs::path base = fs::path("assets/shaders");
            fs::path candidate = base / requested_source;

            auto* data = new IncludeData();
            std::error_code ec;
            if (fs::exists(candidate, ec)) {
                std::ifstream ifs(candidate, std::ios::binary);
                std::ostringstream ss; ss << ifs.rdbuf();
                data->content = ss.str();
                data->source_name = candidate.string();
            } else {
                // Not found: return empty include but still provide a name for diagnostics
                data->content.clear();
                data->source_name = candidate.string();
            }

            auto* result = new shaderc_include_result();
            result->source_name = data->source_name.c_str();
            result->source_name_length = data->source_name.size();
            result->content = data->content.c_str();
            result->content_length = data->content.size();
            result->user_data = data;
            return result;
        }
        void ReleaseInclude(shaderc_include_result* include_result) override {
            auto* data = static_cast<IncludeData*>(include_result->user_data);
            delete data;
            delete include_result;
        }
    };

    options.SetIncluder(std::make_unique<FSIncluder>());

    // Compile
    shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(source.c_str(), source.size(), kind, "shader", entry_point.c_str(), options);

    if (module.GetCompilationStatus() != shaderc_compilation_status_success) {
        HN_CORE_ERROR("[shaderc][{0}] {1}", stage_name, module.GetErrorMessage().c_str());
        return {};
    }

    std::vector<uint32_t> spirv(module.cbegin(), module.cend());
    return spirv;
}

std::string ShaderCompiler::spirv_to_glsl(const std::vector<uint32_t>& spirv_code) {
    if (spirv_code.empty()) {
        return "";
    }

    // Create SPIRV-Cross context
    spvc_context context;
    if (spvc_context_create(&context) != SPVC_SUCCESS) {
        HN_CORE_ERROR("Failed to create SPIRV-Cross context");
        return "";
    }

    // Parse SPIR-V
    spvc_parsed_ir ir;
    if (spvc_context_parse_spirv(context, spirv_code.data(), spirv_code.size(), &ir) != SPVC_SUCCESS) {
        HN_CORE_ERROR("Failed to parse SPIR-V with SPIRV-Cross");
        spvc_context_destroy(context);
        return "";
    }

    // Create GLSL compiler
    spvc_compiler compiler;
    if (spvc_context_create_compiler(context, SPVC_BACKEND_GLSL, ir,
                                     SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &compiler) != SPVC_SUCCESS) {
        HN_CORE_ERROR("Failed to create GLSL compiler");
        spvc_context_destroy(context);
        return "";
    }

    // Set options
    spvc_compiler_options options;
    spvc_compiler_create_compiler_options(compiler, &options);
    spvc_compiler_options_set_uint(options, SPVC_COMPILER_OPTION_GLSL_VERSION, 330);
    spvc_compiler_options_set_bool(options, SPVC_COMPILER_OPTION_GLSL_ES, SPVC_FALSE);
    spvc_compiler_install_compiler_options(compiler, options);

    // Compile to GLSL
    const char* glsl_source;
    if (spvc_compiler_compile(compiler, &glsl_source) != SPVC_SUCCESS) {
        HN_CORE_ERROR("Failed to compile SPIR-V to GLSL");
        spvc_context_destroy(context);
        return "";
    }

    std::string result(glsl_source);
    spvc_context_destroy(context);

    return result;
}

bool ShaderCompiler::validate_spirv(const std::vector<uint32_t>& spirv_code) {
        if (spirv_code.empty()) {
            return false;
        }

        // Basic validation - check SPIR-V magic number
        if (spirv_code[0] != 0x07230203) {
            HN_CORE_ERROR("Invalid SPIR-V magic number");
            return false;
        }

        // Additional validation could be done with SPIRV-Tools
        return true;
    }

ShaderCompiler::ShaderSource ShaderCompiler::parse_shader_file(const std::filesystem::path& path) {
    ShaderSource result;

    std::string source = read_file(path);
    if (source.empty()) {
        HN_CORE_ERROR("Failed to read shader file: {0}", path.string());
        return result;
    }

    // Simple parser for combined vertex/fragment shaders
    // Looks for #type vertex and #type fragment directives
    std::regex type_regex(R"(#type\s+(\w+))");
    std::sregex_iterator it(source.begin(), source.end(), type_regex);
    std::sregex_iterator end_it;

    struct StageMarker { std::string type; size_t marker_pos; };
    std::vector<StageMarker> markers;

    for (; it != end_it; ++it) {
        std::smatch m = *it;
        markers.push_back({ m[1].str(), (size_t)m.position() });
    }

    for (size_t i = 0; i < markers.size(); ++i) {
        size_t header_end = source.find_first_of("\r\n", markers[i].marker_pos);
        if (header_end == std::string::npos)
            header_end = source.size();
        size_t start = source.find_first_not_of("\r\n", header_end);
        if (start == std::string::npos)
            start = source.size();

        size_t end = (i + 1 < markers.size()) ? markers[i + 1].marker_pos : source.size();
        std::string stage_source = source.substr(start, end - start);

        if (markers[i].type == "vertex") {
            result.vertex_source = stage_source;
        } else if (markers[i].type == "fragment" || markers[i].type == "pixel") {
            result.fragment_source = stage_source;
        }
    }

    return result;
}

ShaderCompiler::ShaderStage ShaderCompiler::get_stage_from_string(const std::string& stage_str) {
    if (stage_str == "vertex") return ShaderStage::Vertex;
    if (stage_str == "fragment" || stage_str == "pixel") return ShaderStage::Fragment;
    if (stage_str == "compute") return ShaderStage::Compute;
    if (stage_str == "geometry") return ShaderStage::Geometry;
    if (stage_str == "tess_control") return ShaderStage::TessellationControl;
    if (stage_str == "tess_eval") return ShaderStage::TessellationEvaluation;

    return ShaderStage::Vertex; // Default
}

std::string ShaderCompiler::read_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        HN_CORE_ERROR("Failed to open file: {0}", path.string());
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

}
