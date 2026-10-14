#pragma once

#include <filesystem>
#include <string>

namespace Honey {

    // Single source of truth for where the engine finds assets, config and managed assemblies at
    // runtime. Replaces the old compile-time ASSET_ROOT / CONFIG_ROOT defines and cwd-relative
    // "../assets" paths.
    //
    // Threading: init() runs once from main(), before Application (and therefore TaskSystem) exists.
    // After that every member is read-only, so the getters need no locking.
    //
    // Root resolution order (first candidate containing the asset marker wins):
    //   1. --assets <dir> command-line flag
    //   2. HONEY_ASSET_ROOT environment variable
    //   3. <exe dir>/assets                   (shipped layout)
    //   4. <HN_DEV_SOURCE_ROOT>/assets        (dev builds only)
    class AssetPaths {
    public:
        // Returns false if no asset root could be found; the caller should exit.
        static bool init(int argc, char** argv);

        static const std::filesystem::path& asset_root();
        static const std::filesystem::path& config_root();
        static const std::filesystem::path& cache_dir();      // writable: shader + pipeline caches
        static const std::filesystem::path& user_data_dir();  // writable: settings (4.2 retargets this)
        static std::filesystem::path settings_path();           // user_data_dir()/settings.yaml
        static const std::filesystem::path& executable_dir();

        // Managed (C#) assemblies and the .NET runtime.
        static const std::filesystem::path& engine_assembly_path();
        static const std::filesystem::path& user_scripts_assembly_path();
        static const std::filesystem::path& dotnet_root();

        // Source tree root in dev builds, empty otherwise. Editor-only tooling may use it as a
        // fallback; game code must not.
        static const std::filesystem::path& dev_source_root();

        static bool is_dev_layout();

        // Asset-relative path ("shaders/Foo.glsl") -> absolute path under the asset root.
        // Rejects absolute paths and ".." segments (returns empty) so game data cannot escape the root.
        static std::filesystem::path resolve(const std::filesystem::path& asset_relative);

        // Serialization boundary. Paths are absolute in memory and asset-relative on disk.
        // to_portable: absolute path under the asset root -> "models/Box/Box.gltf". Paths outside
        // the root are kept as-is (generic form).
        // from_portable: stored string -> absolute path. Also accepts legacy cwd-relative
        // "../assets/..." / "assets/..." strings and absolute paths, so old scenes still load.
        static std::string to_portable(const std::filesystem::path& path);
        static std::filesystem::path from_portable(const std::string& stored);
    };

}