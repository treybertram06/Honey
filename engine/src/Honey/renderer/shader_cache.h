#pragma once

#include "Honey/core/base.h"
#include "shader.h"

#include <filesystem>
#include <unordered_map>
#include <memory>

namespace Honey {
    class ShaderCache {
        struct ShaderAsset {
            std::filesystem::path source_path;
            std::filesystem::path vertex_spirv_path;
            std::filesystem::path fragment_spirv_path;
            std::filesystem::file_time_type last_modified;
            Ref<Shader> cached_shader;
        };

    public:
        explicit ShaderCache(const std::filesystem::path& cache_dir = "assets/cache/shaders");
        ~ShaderCache() = default;

        // non-copyable but movable
        ShaderCache(const ShaderCache&) = delete;
        ShaderCache& operator=(const ShaderCache&) = delete;
        ShaderCache(ShaderCache&&) = default;
        ShaderCache& operator=(ShaderCache&&) = default;

        Ref<Shader> get_or_compile_shader(const std::filesystem::path& shader_path);
        void invalidate_cache();
        void precompile_all_shaders();
        void set_spirv_cache_directory(const std::filesystem::path& cache_dir);


    private:
        bool needs_recompilation(const ShaderAsset& asset);
        void compile_shader_to_spirv(const std::filesystem::path& shader_path);
        std::filesystem::path get_spirv_cache_path(const std::filesystem::path& shader_path, const std::string& stage);

        std::unordered_map<std::string, ShaderAsset> m_shader_assets;
        std::filesystem::path m_spirv_cache_dir;
    };
}