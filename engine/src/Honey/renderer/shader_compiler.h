#pragma once

#include "hnpch.h"
#include <filesystem>
#include <vector>
#include <string>

#include "vendor/spirv-headers/include/spirv/unified1/spirv.hpp"


// SPIRV-Cross includes
extern "C" {
#include <spirv_cross_c.h>
}

namespace Honey {

    class ShaderCompiler {
    public:
        enum class ShaderStage {
            Unknown = 0,

            Vertex,
            Fragment,
            Compute,
            Geometry,
            TessellationControl,
            TessellationEvaluation,
            Task,
            Mesh,
            RayGen,
            Miss,
            ClosestHit,
        };

        static ShaderStage get_stage_from_spirv_execution_model(spv::ExecutionModel execution_model);

        struct CompilationResult {
            std::vector<uint32_t> vertex_spirv;
            std::vector<uint32_t> fragment_spirv;
            std::vector<uint32_t> compute_spirv;
            std::vector<uint32_t> task_spirv;
            std::vector<uint32_t> mesh_spirv;
            bool success = false;
            std::string error_message;

            bool has_graphics_stages() const {
                return !vertex_spirv.empty() || !fragment_spirv.empty();
            }

            bool has_compute_stage() const {
                return !compute_spirv.empty();
            }

            bool has_mesh_stages() const {
                return !mesh_spirv.empty() && !fragment_spirv.empty();
            }

            bool has_task_stage() const {
                return !task_spirv.empty() && has_mesh_stages();
            }
        };

        static CompilationResult compile_glsl_to_spirv(const std::filesystem::path& shader_path);
        static CompilationResult compile_hlsl_to_spirv(const std::filesystem::path& shader_path);

        // Additional utility methods
        static std::vector<uint32_t> compile_single_stage(const std::string& source,
                                                          ShaderStage stage,
                                                          const std::string& entry_point = "main");

        static std::string spirv_to_glsl(const std::vector<uint32_t>& spirv_code);
        static bool validate_spirv(const std::vector<uint32_t>& spirv_code);

        static std::string read_file(const std::filesystem::path& path);

    private:
        struct ShaderSource {
            std::string vertex_source;
            std::string fragment_source;
            std::string compute_source;
            std::string task_source;
            std::string mesh_source;
            std::string raygen_source;
            std::string miss_source;
            std::string closest_hit_source;
        };

        static ShaderSource parse_shader_file(const std::filesystem::path& path);
        static ShaderStage get_stage_from_string(const std::string& stage_str);
    };

} // namespace Honey