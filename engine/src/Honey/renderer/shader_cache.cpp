#include "hnpch.h"
#include "shader_cache.h"
#include "Honey/core/log.h"
#include "shader_compiler.h"

#include <fstream>
#include <sstream>

#include "renderer.h"

namespace {
    constexpr uint32_t kShaderCacheVersion = 1;

    static std::string read_text_file(const std::filesystem::path& p) {
        std::ifstream ifs(p, std::ios::binary);
        if (!ifs) return {};
        std::ostringstream ss; ss << ifs.rdbuf();
        return ss.str();
    }

    static std::string fnv1a64_hex(const std::string& s) {
        uint64_t h = 1469598103934665603ull;
        for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
        std::ostringstream oss; oss << std::hex << h;
        return oss.str();
    }

    static bool file_exists_nonempty(const std::filesystem::path& p) {
        std::error_code ec;
        if (!std::filesystem::exists(p, ec) || ec) return false;
        auto sz = std::filesystem::file_size(p, ec);
        if (ec) return false;
        return sz > 0;
    }
}

namespace Honey {
    ShaderCache::ShaderCache(const std::filesystem::path& cache_dir)
        : m_spirv_cache_dir(cache_dir)
    {
        // Create cache directory if it doesn't exist
        try {
            std::filesystem::create_directories(m_spirv_cache_dir);
            HN_CORE_INFO("ShaderCache initialized with cache directory: {0}", m_spirv_cache_dir.string());
        } catch (const std::filesystem::filesystem_error& e) {
            HN_CORE_ERROR("Failed to create shader cache directory: {0}", e.what());
        }
    }

    void ShaderCache::set_spirv_cache_directory(const std::filesystem::path& cache_dir) {
        m_spirv_cache_dir = cache_dir;

        // Create cache directory if it doesn't exist
        try {
            std::filesystem::create_directories(m_spirv_cache_dir);
        } catch (const std::filesystem::filesystem_error& e) {
            HN_CORE_ERROR("Failed to create shader cache directory: {0}", e.what());
        }
    }

    Ref<Shader> ShaderCache::get_or_compile_shader(const std::filesystem::path& shader_path) {
        std::string shader_key = shader_path.string();

        // If we have a valid in-memory cached shader, return it.
        auto it = m_shader_assets.find(shader_key);
        if (it != m_shader_assets.end()) {
            ShaderAsset& asset = it->second;
            if (!needs_recompilation(asset) && asset.cached_shader) {
                return asset.cached_shader;
            }
        }

        // Disk-first: if the expected SPIR-V cache files exist, reuse them without compiling.
        const auto vert_path = get_spirv_cache_path(shader_path, "vert");
        const auto frag_path = get_spirv_cache_path(shader_path, "frag");

        if (file_exists_nonempty(vert_path) && file_exists_nonempty(frag_path)) {
            ShaderAsset asset;
            asset.source_path = shader_path;
            asset.vertex_spirv_path = vert_path;
            asset.fragment_spirv_path = frag_path;

            // Best-effort timestamp (not required for correctness anymore)
            std::error_code ec;
            asset.last_modified = std::filesystem::last_write_time(shader_path, ec);

            if (Renderer::get_api() == RendererAPI::API::opengl) {
                asset.cached_shader = Shader::create_from_spirv_files(vert_path, frag_path);
                if (!asset.cached_shader) {
                    HN_CORE_ERROR("Failed to create shader from cached SPIR-V: {0}", shader_path.string());
                    return nullptr;
                }
            } else {
                asset.cached_shader = nullptr; // Vulkan: shaders are used via pipeline creation
            }

            m_shader_assets[shader_key] = std::move(asset);
            return m_shader_assets[shader_key].cached_shader;
        }

        // Fallback: compile (no usable cache entry on disk).
        HN_CORE_INFO("Compiling shader: {0}", shader_path.string());

        try {
            compile_shader_to_spirv(shader_path);

            ShaderAsset asset;
            asset.source_path = shader_path;
            asset.vertex_spirv_path = vert_path;
            asset.fragment_spirv_path = frag_path;
            asset.last_modified = std::filesystem::last_write_time(shader_path);
            asset.cached_shader = nullptr;

            if (Renderer::get_api() == RendererAPI::API::opengl) {
                asset.cached_shader = Shader::create_from_spirv_files(vert_path, frag_path);
                if (!asset.cached_shader) {
                    HN_CORE_ERROR("Failed to create shader from SPIR-V: {0}", shader_path.stem().string());
                    return nullptr;
                }
            }

            m_shader_assets[shader_key] = std::move(asset);
            return m_shader_assets[shader_key].cached_shader;

        } catch (const std::exception& e) {
            HN_CORE_ERROR("Shader compilation failed for {0}: {1}", shader_path.string(), e.what());
            return nullptr;
        }
    }

    ShaderCache::SpirvPaths ShaderCache::get_or_compile_spirv_paths(const std::filesystem::path& shader_path) {
        const auto vert_path = get_spirv_cache_path(shader_path, "vert");
        const auto frag_path = get_spirv_cache_path(shader_path, "frag");

        if (file_exists_nonempty(vert_path) && file_exists_nonempty(frag_path)) {
            HN_CORE_INFO("Shader cache hit (SPIR-V only): {0}", shader_path.string());
            return { vert_path, frag_path };
        }

        std::string shader_key = shader_path.string();

        auto it = m_shader_assets.find(shader_key);
        if (it == m_shader_assets.end() || needs_recompilation(it->second)) {
            HN_CORE_INFO("Compiling shader (SPIR-V only): {0}", shader_path.string());
            compile_shader_to_spirv(shader_path);

            ShaderAsset asset;
            asset.source_path = shader_path;
            asset.vertex_spirv_path = vert_path;
            asset.fragment_spirv_path = frag_path;
            asset.last_modified = std::filesystem::last_write_time(shader_path);
            asset.cached_shader = nullptr;

            m_shader_assets[shader_key] = std::move(asset);
            it = m_shader_assets.find(shader_key);
        }

        HN_CORE_ASSERT(it != m_shader_assets.end(), "ShaderCache: failed to create/find shader asset entry");
        HN_CORE_ASSERT(std::filesystem::exists(it->second.vertex_spirv_path), "Missing cached vertex SPIR-V");
        HN_CORE_ASSERT(std::filesystem::exists(it->second.fragment_spirv_path), "Missing cached fragment SPIR-V");

        return { it->second.vertex_spirv_path, it->second.fragment_spirv_path };
    }

    bool ShaderCache::needs_recompilation(const ShaderAsset& asset) {
        try {
            // Check if source file was modified
            auto current_time = std::filesystem::last_write_time(asset.source_path);
            if (current_time > asset.last_modified) {
                return true;
            }

            // Basic include-change detection: if any file in the same directory is newer, recompile.
            try {
                auto dir = asset.source_path.parent_path();
                for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                    if (!entry.is_regular_file()) continue;
                    auto t = std::filesystem::last_write_time(entry.path());
                    if (t > asset.last_modified) {
                        return true;
                    }
                }
            } catch (const std::exception&) {
                // Ignore directory scan errors and fall back to existing checks
            }

            // Check if SPIR-V files exist
            if (!std::filesystem::exists(asset.vertex_spirv_path) ||
                !std::filesystem::exists(asset.fragment_spirv_path)) {
                return true;
            }

            return false;

        } catch (const std::filesystem::filesystem_error& e) {
            HN_CORE_WARN("Error checking shader modification time: {0}", e.what());
            return true; // Assume needs recompilation on error
        }
    }

    void ShaderCache::compile_shader_to_spirv(const std::filesystem::path& shader_path) {
        auto result = ShaderCompiler::compile_glsl_to_spirv(shader_path);

        if (!result.success) {
            throw std::runtime_error("Shader compilation failed: " + result.error_message);
        }

        // Write SPIR-V files to cache
        auto vert_path = get_spirv_cache_path(shader_path, "vert");
        auto frag_path = get_spirv_cache_path(shader_path, "frag");

        try {
            // Write vertex SPIR-V
            std::ofstream vert_file(vert_path, std::ios::binary);
            if (!vert_file) {
                throw std::runtime_error("Failed to open vertex SPIR-V file for writing");
            }
            vert_file.write(reinterpret_cast<const char*>(result.vertex_spirv.data()),
                           result.vertex_spirv.size() * sizeof(uint32_t));
            vert_file.close();

            // Write fragment SPIR-V
            std::ofstream frag_file(frag_path, std::ios::binary);
            if (!frag_file) {
                throw std::runtime_error("Failed to open fragment SPIR-V file for writing");
            }
            frag_file.write(reinterpret_cast<const char*>(result.fragment_spirv.data()),
                           result.fragment_spirv.size() * sizeof(uint32_t));
            frag_file.close();

            HN_CORE_INFO("SPIR-V cache written: {0}, {1}", vert_path.string(), frag_path.string());

        } catch (const std::exception& e) {
            throw std::runtime_error("Failed to write SPIR-V cache: " + std::string(e.what()));
        }
    }

    std::filesystem::path ShaderCache::get_spirv_cache_path(const std::filesystem::path& shader_path, const std::string& stage) {
        std::string base = shader_path.stem().string();

        std::string contents = read_text_file(shader_path);

        // Include compile target in the hash so Vulkan/OpenGL don't collide.
        std::string target_tag = "unknown";
        switch (Renderer::get_api()) {
        case RendererAPI::API::opengl: target_tag = "opengl"; break;
        case RendererAPI::API::vulkan: target_tag = "vulkan"; break;
        default: break;
        }

        std::string hash_input = target_tag + "\n" + contents;
        std::string hash = hash_input.empty() ? std::string("0") : fnv1a64_hex(hash_input);

        std::string filename = base + ".v" + std::to_string(kShaderCacheVersion) + "." + hash + "." + stage + ".spv";
        return m_spirv_cache_dir / filename;
    }

    void ShaderCache::invalidate_cache() {
        HN_CORE_INFO("Invalidating shader cache");

        for (auto& [key, asset] : m_shader_assets) {
            asset.cached_shader.reset();
        }
        m_shader_assets.clear();
    }

    void ShaderCache::precompile_all_shaders() {
        HN_CORE_INFO("Precompiling all shaders...");

        // This could scan the assets/shaders directory and precompile everything
        for (auto& [key, asset] : m_shader_assets) {
            try {
                compile_shader_to_spirv(asset.source_path);
                HN_CORE_INFO("Precompiled: {0}", asset.source_path.string());
            } catch (const std::exception& e) {
                HN_CORE_ERROR("Failed to precompile {0}: {1}", asset.source_path.string(), e.what());
            }
        }
    }
}