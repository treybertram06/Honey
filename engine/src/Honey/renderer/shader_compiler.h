#pragma once

#include "hnpch.h"
#include <filesystem>
#include <vector>
#include <string>

// SPIRV-Cross includes
extern "C" {
#include <spirv_cross_c.h>
}

namespace Honey {

    class ShaderCompiler {
    public:
        enum class ShaderStage {
            Vertex,
            Fragment,
            Compute,
            Geometry,
            TessellationControl,
            TessellationEvaluation
        };

        struct CompilationResult {
            std::vector<uint32_t> vertex_spirv;
            std::vector<uint32_t> fragment_spirv;
            bool success = false;
            std::string error_message;
        };

        static CompilationResult compile_glsl_to_spirv(const std::filesystem::path& shader_path);
        static CompilationResult compile_hlsl_to_spirv(const std::filesystem::path& shader_path);

        // Additional utility methods
        static std::vector<uint32_t> compile_single_stage(const std::string& source,
                                                          ShaderStage stage,
                                                          const std::string& entry_point = "main");

        static std::string spirv_to_glsl(const std::vector<uint32_t>& spirv_code);
        static bool validate_spirv(const std::vector<uint32_t>& spirv_code);

    private:
        struct ShaderSource {
            std::string vertex_source;
            std::string fragment_source;
        };

        static ShaderSource parse_shader_file(const std::filesystem::path& path);
        static ShaderStage get_stage_from_string(const std::string& stage_str);
        static std::string read_file(const std::filesystem::path& path);
    };

} // namespace Honey