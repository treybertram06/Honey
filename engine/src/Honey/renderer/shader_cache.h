#pragma once

#include "Honey/core/base.h"
#include "shader.h"

#include <filesystem>
#include <unordered_map>

namespace Honey {

    class ShaderCache {
    public:
        ShaderCache(const std::filesystem::path& cache_dir = "assets/cache/shaders");

        void set_spirv_cache_directory(const std::filesystem::path& cache_dir);

        Ref<Shader> get_or_compile_shader(const std::filesystem::path& shader_path);

        struct SpirvPaths {
            std::filesystem::path vertex;
            std::filesystem::path fragment;
        };

        SpirvPaths get_or_compile_spirv_paths(const std::filesystem::path& shader_path);

        void invalidate_cache();
        void precompile_all_shaders();

    private:
        struct ShaderAsset {
            std::filesystem::path source_path;
            std::filesystem::path vertex_spirv_path;
            std::filesystem::path fragment_spirv_path;
            std::filesystem::file_time_type last_modified{};
            Ref<Shader> cached_shader;
        };

        bool needs_recompilation(const ShaderAsset& asset);
        void compile_shader_to_spirv(const std::filesystem::path& shader_path);
        std::filesystem::path get_spirv_cache_path(const std::filesystem::path& shader_path, const std::string& stage);

        std::filesystem::path m_spirv_cache_dir;
        std::unordered_map<std::string, ShaderAsset> m_shader_assets;
    };

} // namespace Honey