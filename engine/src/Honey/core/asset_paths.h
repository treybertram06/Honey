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
    // Two roots (phase 1.5): engine content (shaders, frame graphs, default fonts/textures; needed by
    // editor, player and every project) and the asset root (today's stop-gap project content).
    // Engine content is addressed in paths with the "engine:" prefix ("engine:fonts/X.ttf").
    //
    // Project (phase 1.5): `--project <dir|file.honeyproj>` (or shipped `<exe>/game/*.honeyproj`) selects a
    // portable project folder {Name.honeyproj, Assets/, Scripts/}. Its Assets/ becomes the asset root,
    // its .honeyproj is the manifest, Scripts/ holds the C# project. Without one, the legacy stop-gap
    // layout applies (asset root below, scripts in <asset root>/scripts, manifest at <asset root>/../game.yaml).
    //
    // Asset root resolution when no project is given (first existing directory wins):
    //   1. --assets <dir> command-line flag
    //   2. HONEY_ASSET_ROOT environment variable
    //   3. <exe dir>/assets                   (shipped layout)
    //   4. <HN_DEV_SOURCE_ROOT>/assets        (dev builds only)
    // Engine content root resolution (first directory containing shaders/ and frame_graphs/ wins):
    //   1. --engine-content <dir> command-line flag
    //   2. HONEY_ENGINE_CONTENT environment variable
    //   3. <exe dir>/engine_content           (shipped layout)
    //   4. <HN_DEV_SOURCE_ROOT>/engine_content (dev builds only)
    class AssetPaths {
    public:
        // Returns false if no asset root could be found; the caller should exit.
        static bool init(int argc, char** argv);

        static const std::filesystem::path& asset_root();
        static const std::filesystem::path& engine_content_root();
        static bool has_project();                                 // false = legacy stop-gap layout
        static const std::filesystem::path& project_root();        // empty when !has_project()
        static const std::filesystem::path& scripts_dir();         // C# project dir (UserScripts.csproj)
        static const std::filesystem::path& config_root();
        static const std::filesystem::path& cache_dir();      // writable, per user: user_data_dir()/cache (shader + pipeline caches)
        static const std::filesystem::path& user_data_dir();  // writable: settings (4.2 retargets this)
        static const std::filesystem::path& manifest_path();
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

        // Prefix marking a path as engine content rather than project/asset-relative.
        static constexpr const char* k_engine_prefix = "engine:";
        static bool is_engine_path(const std::string& s) { return s.rfind(k_engine_prefix, 0) == 0; }

        // Asset-relative path ("textures/Foo.png") -> absolute path under the asset root;
        // "engine:fonts/Foo.ttf" -> absolute path under the engine content root.
        // Rejects absolute paths and ".." segments (returns empty) so game data cannot escape the root.
        static std::filesystem::path resolve(const std::filesystem::path& asset_relative);

        // Serialization boundary. Paths are absolute in memory and asset-relative on disk.
        // to_portable: absolute path under the asset root -> "models/Box/Box.gltf"; under the engine
        // content root -> "engine:fonts/X.ttf". Paths outside both are kept as-is (generic form).
        // from_portable: stored string -> absolute path. Also accepts legacy cwd-relative
        // "../assets/..." / "assets/..." strings and absolute paths, so old scenes still load.
        static std::string to_portable(const std::filesystem::path& path);
        static std::filesystem::path from_portable(const std::string& stored);

    private:
        static std::filesystem::path resolve_under(const std::filesystem::path& root,
                                                   const std::filesystem::path& relative,
                                                   const std::filesystem::path& original);
    };

}
