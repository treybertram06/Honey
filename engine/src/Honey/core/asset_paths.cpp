#include "hnpch.h"
#include "asset_paths.h"

#include "Honey/utils/platform_utils.h"

#include <cstdlib>
#include <optional>
#include <string_view>
#include <vector>

namespace Honey {

    namespace fs = std::filesystem;

    namespace {
        struct AssetPathData {
            bool initialized = false;
            bool dev_layout = false;
            fs::path executable_dir;
            fs::path dev_source_root;
            fs::path asset_root;
            fs::path config_root;
            fs::path cache_dir;
            fs::path user_data_dir;
            fs::path engine_assembly;
            fs::path user_scripts_assembly;
            fs::path dotnet_root;
        };

        AssetPathData s_data;

        const fs::path& checked(const fs::path& p) {
            HN_CORE_ASSERT(s_data.initialized, "AssetPaths used before AssetPaths::init() (static initializer?)");
            return p;
        }

        // A directory is only accepted as the asset root if it looks like one. Checking existence alone
        // would happily accept an unrelated "assets" folder and fail much later on a missing shader.
        bool is_asset_root(const fs::path& dir, std::string& why_not) {
            std::error_code ec;
            if (!fs::is_directory(dir, ec)) {
                why_not = "not a directory";
                return false;
            }
            if (!fs::is_directory(dir / "shaders", ec) || !fs::is_directory(dir / "frame_graphs", ec)) {
                why_not = "missing shaders/ or frame_graphs/";
                return false;
            }
            return true;
        }

        fs::path normalized(const fs::path& p) {
            std::error_code ec;
            fs::path out = fs::weakly_canonical(p, ec);
            return ec ? p.lexically_normal() : out;
        }

        std::optional<fs::path> find_cli_assets_arg(int argc, char** argv) {
            for (int i = 1; i + 1 < argc; ++i) {
                if (std::string_view(argv[i]) == "--assets")
                    return fs::path(argv[i + 1]);
            }
            return std::nullopt;
        }

        bool has_parent_segment(const fs::path& p) {
            for (const auto& part : p) {
                if (part == "..")
                    return true;
            }
            return false;
        }
    }

    bool AssetPaths::init(int argc, char** argv) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(!s_data.initialized, "AssetPaths::init called twice");

        s_data.executable_dir = Platform::get_executable_path().parent_path();
#ifdef HN_DEV_SOURCE_ROOT
        s_data.dev_source_root = normalized(fs::path(HN_DEV_SOURCE_ROOT));
#endif

        struct Candidate { fs::path dir; const char* rule; };
        std::vector<Candidate> candidates;
        if (auto cli = find_cli_assets_arg(argc, argv))
            candidates.push_back({ *cli, "cli --assets" });
        if (const char* env = std::getenv("HONEY_ASSET_ROOT"); env && *env)
            candidates.push_back({ fs::path(env), "env HONEY_ASSET_ROOT" });
        if (!s_data.executable_dir.empty())
            candidates.push_back({ s_data.executable_dir / "assets", "shipped (exe dir)" });
        if (!s_data.dev_source_root.empty())
            candidates.push_back({ s_data.dev_source_root / "assets", "dev source tree" });

        const Candidate* chosen = nullptr;
        std::string failures;
        for (const auto& c : candidates) {
            std::string why_not;
            if (is_asset_root(c.dir, why_not)) {
                chosen = &c;
                break;
            }
            failures += fmt::format("\n  [{}] {} -> {}", c.rule, c.dir.string(), why_not);
        }

        if (!chosen) {
            HN_CORE_FATAL("AssetPaths: no asset root found. Tried:{}", failures);
            return false;
        }

        s_data.asset_root = normalized(chosen->dir);
        s_data.config_root = s_data.asset_root.parent_path() / "config";
        s_data.cache_dir = s_data.asset_root / "cache";
        s_data.user_data_dir = s_data.config_root;

        // Managed assemblies: a shipped layout puts them next to the executable; otherwise use the
        // dotnet build output locations in the source tree.
        const fs::path shipped_managed = s_data.executable_dir / "managed";
        std::error_code ec;
        if (fs::exists(shipped_managed / "HoneyEngine.dll", ec)) {
            s_data.engine_assembly = shipped_managed / "HoneyEngine.dll";
            s_data.user_scripts_assembly = shipped_managed / "UserScripts.dll";
        } else {
            s_data.dev_layout = true;
            const fs::path source_root = !s_data.dev_source_root.empty()
                ? s_data.dev_source_root
                : s_data.asset_root.parent_path();
            s_data.engine_assembly = source_root / "Honey/managed/HoneyEngine/bin/Release/net10.0/HoneyEngine.dll";
            s_data.user_scripts_assembly = s_data.asset_root / "scripts/bin/Debug/net10.0/UserScripts.dll";
        }

#ifdef HN_DOTNET_RUNTIME_ROOT
        // Still baked at configure time; 4.3 moves the vendored runtime next to the executable.
        s_data.dotnet_root = fs::path(HN_DOTNET_RUNTIME_ROOT).lexically_normal();
#endif

        s_data.initialized = true;

        HN_CORE_INFO("AssetPaths: asset root = {} (rule: {})", s_data.asset_root.string(), chosen->rule);
        HN_CORE_INFO("AssetPaths: config root = {}, managed layout = {}", s_data.config_root.string(),
                     s_data.dev_layout ? "dev" : "shipped");
        return true;
    }

    const fs::path& AssetPaths::asset_root()                 { return checked(s_data.asset_root); }
    const fs::path& AssetPaths::config_root()                { return checked(s_data.config_root); }
    const fs::path& AssetPaths::cache_dir()                  { return checked(s_data.cache_dir); }
    const fs::path& AssetPaths::user_data_dir()              { return checked(s_data.user_data_dir); }
    const fs::path& AssetPaths::executable_dir()             { return checked(s_data.executable_dir); }
    const fs::path& AssetPaths::engine_assembly_path()       { return checked(s_data.engine_assembly); }
    const fs::path& AssetPaths::user_scripts_assembly_path() { return checked(s_data.user_scripts_assembly); }
    const fs::path& AssetPaths::dotnet_root()                { return checked(s_data.dotnet_root); }
    const fs::path& AssetPaths::dev_source_root()            { return checked(s_data.dev_source_root); }

    fs::path AssetPaths::settings_path() {
        return checked(s_data.user_data_dir) / "settings.yaml";
    }

    bool AssetPaths::is_dev_layout() {
        checked(s_data.asset_root);
        return s_data.dev_layout;
    }

    fs::path AssetPaths::resolve(const fs::path& asset_relative) {
        const fs::path& root = checked(s_data.asset_root);
        if (asset_relative.empty())
            return {};
        if (asset_relative.is_absolute() || has_parent_segment(asset_relative)) {
            HN_CORE_WARN("AssetPaths::resolve: rejected non asset-relative path '{}'", asset_relative.generic_string());
            return {};
        }
        return (root / asset_relative).lexically_normal();
    }

    std::string AssetPaths::to_portable(const fs::path& path) {
        const fs::path& root = checked(s_data.asset_root);
        if (path.empty())
            return {};

        // Relative input is already in some stored form; round-trip it through from_portable so
        // legacy "../assets/x" is rewritten as "x".
        const fs::path absolute = path.is_absolute() ? path.lexically_normal() : from_portable(path.generic_string());

        const fs::path relative = absolute.lexically_relative(root);
        if (relative.empty() || has_parent_segment(relative))
            return absolute.generic_string(); // outside the asset root; keep as-is

        return relative.generic_string();
    }

    fs::path AssetPaths::from_portable(const std::string& stored) {
        const fs::path& root = checked(s_data.asset_root);
        if (stored.empty())
            return {};

        fs::path p(stored);
        if (p.is_absolute())
            return p.lexically_normal();

        // Legacy scenes stored cwd-relative paths ("../assets/x" from cmake-build-debug, or "assets/x").
        // Strip a leading run of ".." segments followed by "assets"; anything else is asset-relative.
        auto it = p.begin();
        while (it != p.end() && *it == "..")
            ++it;
        if (it != p.end() && *it == "assets") {
            fs::path rest;
            for (++it; it != p.end(); ++it)
                rest /= *it;
            return (root / rest).lexically_normal();
        }

        return (root / p).lexically_normal();
    }

}