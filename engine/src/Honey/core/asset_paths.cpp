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
            fs::path engine_content_root;
            fs::path project_root;
            fs::path scripts_dir;
            fs::path config_root;
            fs::path cache_dir;
            fs::path user_data_dir;
            fs::path manifest_dir;
            fs::path engine_assembly;
            fs::path user_scripts_assembly;
            fs::path dotnet_root;
        };

        AssetPathData s_data;

        const fs::path& checked(const fs::path& p) {
            HN_CORE_ASSERT(s_data.initialized, "AssetPaths used before AssetPaths::init() (static initializer?)");
            return p;
        }

        // A directory is only accepted as the engine content root if it looks like one. Checking
        // existence alone would accept an unrelated folder and fail much later on a missing shader.
        bool is_engine_content_root(const fs::path& dir, std::string& why_not) {
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

        bool is_asset_root(const fs::path& dir, std::string& why_not) {
            std::error_code ec;
            if (!fs::is_directory(dir, ec)) {
                why_not = "not a directory";
                return false;
            }
            return true;
        }

        std::optional<fs::path> find_cli_arg(int argc, char** argv, std::string_view flag) {
            for (int i = 1; i + 1 < argc; ++i) {
                if (std::string_view(argv[i]) == flag)
                    return fs::path(argv[i + 1]);
            }
            return std::nullopt;
        }

        std::optional<fs::path> find_cli_assets_arg(int argc, char** argv) {
            return find_cli_arg(argc, argv, "--assets");
        }

        std::optional<fs::path> find_cli_manifest_arg(int argc, char** argv) {
            for (int i = 1; i + 1 < argc; ++i) {
                if (std::string_view(argv[i]) == "--manifest")
                    return fs::path(argv[i + 1]);
            }
            return std::nullopt;
        }

        // `--project` accepts a project folder or the .honeyproj file itself. Returns the .honeyproj path.
        std::optional<fs::path> find_project_file(const fs::path& arg, std::string& why_not) {
            std::error_code ec;
            if (fs::is_regular_file(arg, ec)) {
                if (arg.extension() != ".honeyproj") { why_not = "not a .honeyproj file"; return std::nullopt; }
                return arg;
            }
            if (!fs::is_directory(arg, ec)) { why_not = "no such file or directory"; return std::nullopt; }
            std::vector<fs::path> found;
            for (const auto& e : fs::directory_iterator(arg, ec)) {
                if (e.is_regular_file() && e.path().extension() == ".honeyproj")
                    found.push_back(e.path());
            }
            if (found.empty()) { why_not = "no .honeyproj file in folder"; return std::nullopt; }
            if (found.size() > 1) { why_not = "more than one .honeyproj file in folder"; return std::nullopt; }
            return found.front();
        }

        bool has_parent_segment(const fs::path& p) {
            for (const auto& part : p) {
                if (part == "..")
                    return true;
            }
            return false;
        }

        // Points the project-dependent paths at `file`'s folder. All-or-nothing: on failure nothing changes.
        bool apply_project(const fs::path& file, std::string& error) {
            const fs::path root = normalized(file.parent_path());
            std::error_code ec;
            if (!fs::is_directory(root / "Assets", ec)) {
                error = "project has no Assets/ folder";
                return false;
            }
            s_data.project_root = root;
            s_data.asset_root = root / "Assets";
            s_data.scripts_dir = root / "Scripts";
            s_data.manifest_dir = normalized(file);
            // A shipped layout loads a prebuilt UserScripts.dll next to the executable; the dev layout
            // loads whatever `dotnet build` produced in the project.
            if (s_data.dev_layout)
                s_data.user_scripts_assembly = s_data.scripts_dir / "bin/Debug/net10.0/UserScripts.dll";
            return true;
        }

        void clear_project() {
            s_data.project_root.clear();
            s_data.asset_root.clear();
            s_data.scripts_dir.clear();
            s_data.manifest_dir.clear();
            if (s_data.dev_layout)
                s_data.user_scripts_assembly.clear();
        }
    }

    bool AssetPaths::init(int argc, char** argv, Mode mode) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(!s_data.initialized, "AssetPaths::init called twice");

        s_data.executable_dir = Platform::get_executable_path().parent_path();
#ifdef HN_DEV_SOURCE_ROOT
        s_data.dev_source_root = normalized(fs::path(HN_DEV_SOURCE_ROOT));
#endif

        struct Candidate { fs::path dir; const char* rule; };

        // Project (if any) decides the asset root; otherwise the legacy candidates below do.
        std::optional<fs::path> project_arg;
        const char* project_rule = nullptr;
        if (auto cli = find_cli_arg(argc, argv, "--project")) {
            project_arg = *cli;
            project_rule = "cli --project";
        } else if (!s_data.executable_dir.empty()) {
            std::string ignored;
            if (auto shipped = find_project_file(s_data.executable_dir / "game", ignored)) {
                project_arg = shipped->parent_path();
                project_rule = "shipped (exe dir/game)";
            }
        }

        std::string asset_rule = "none (no project open)";
        std::error_code ec;
        // dev_layout decides where UserScripts.dll lives; it is settled below, but apply_project needs it,
        // so decide it first.
        s_data.dev_layout = !fs::exists(s_data.executable_dir / "managed" / "HoneyEngine.dll", ec);
        if (project_arg) {
            std::string why_not;
            auto file = find_project_file(*project_arg, why_not);
            if (!file) {
                HN_CORE_FATAL("[AssetPaths] Cannot open project '{}': {}", project_arg->string(), why_not);
                return false;
            }
            if (!apply_project(*file, why_not)) {
                HN_CORE_FATAL("[AssetPaths] Project '{}': {}", file->parent_path().string(), why_not);
                return false;
            }
            asset_rule = std::string("project (") + project_rule + ")";
        } else {
            // Legacy stop-gap layout (removed in phase 1.5 step 5). The editor only honours an explicit
            // --assets; the player also probes the environment / shipped / dev locations.
            std::vector<Candidate> candidates;
            if (auto cli = find_cli_assets_arg(argc, argv))
                candidates.push_back({ *cli, "cli --assets" });
            if (mode == Mode::player) {
                if (const char* env = std::getenv("HONEY_ASSET_ROOT"); env && *env)
                    candidates.push_back({ fs::path(env), "env HONEY_ASSET_ROOT" });
                if (!s_data.executable_dir.empty())
                    candidates.push_back({ s_data.executable_dir / "assets", "shipped (exe dir)" });
                if (!s_data.dev_source_root.empty())
                    candidates.push_back({ s_data.dev_source_root / "assets", "dev source tree" });
            }

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
            if (chosen) {
                s_data.asset_root = normalized(chosen->dir);
                s_data.scripts_dir = s_data.asset_root / "scripts";
                asset_rule = chosen->rule;
            } else if (mode == Mode::player) {
                HN_CORE_FATAL("[AssetPaths] No asset root found, tried:{}", failures);
                return false;
            }
            // Editor with nothing to open: start with no project (welcome state).
        }

        // Engine content root.
        std::vector<Candidate> ec_candidates;
        if (auto cli = find_cli_arg(argc, argv, "--engine-content"))
            ec_candidates.push_back({ *cli, "cli --engine-content" });
        if (const char* env = std::getenv("HONEY_ENGINE_CONTENT"); env && *env)
            ec_candidates.push_back({ fs::path(env), "env HONEY_ENGINE_CONTENT" });
        if (!s_data.executable_dir.empty())
            ec_candidates.push_back({ s_data.executable_dir / "engine_content", "shipped (exe dir)" });
        if (!s_data.dev_source_root.empty())
            ec_candidates.push_back({ s_data.dev_source_root / "engine_content", "dev source tree" });

        const Candidate* ec_chosen = nullptr;
        std::string ec_failures;
        for (const auto& c : ec_candidates) {
            std::string why_not;
            if (is_engine_content_root(c.dir, why_not)) {
                ec_chosen = &c;
                break;
            }
            ec_failures += fmt::format("\n  [{}] {} -> {}", c.rule, c.dir.string(), why_not);
        }
        if (!ec_chosen) {
            HN_CORE_FATAL("[AssetPaths] No engine content root found, tried:{}", ec_failures);
            return false;
        }
        s_data.engine_content_root = normalized(ec_chosen->dir);
        // Per-user data must not depend on the project: it sits beside the engine (source tree in dev
        // builds, next to the executable when shipped).
        s_data.config_root = (s_data.dev_source_root.empty() ? s_data.executable_dir : s_data.dev_source_root) / "config";
        s_data.user_data_dir = s_data.config_root;
        s_data.cache_dir = s_data.user_data_dir / "cache";

        // Managed assemblies: a shipped layout puts them next to the executable; otherwise use the
        // dotnet build output locations in the source tree.
        const fs::path shipped_managed = s_data.executable_dir / "managed";
        if (!s_data.dev_layout) {
            s_data.engine_assembly = shipped_managed / "HoneyEngine.dll";
            s_data.user_scripts_assembly = shipped_managed / "UserScripts.dll";
        } else {
            const fs::path source_root = !s_data.dev_source_root.empty()
                ? s_data.dev_source_root
                : s_data.asset_root.parent_path();
            s_data.engine_assembly = source_root / "Honey/managed/HoneyEngine/bin/Release/net10.0/HoneyEngine.dll";
            if (!s_data.scripts_dir.empty())
                s_data.user_scripts_assembly = s_data.scripts_dir / "bin/Debug/net10.0/UserScripts.dll";
        }

#ifdef HN_DOTNET_RUNTIME_ROOT
        // Still baked at configure time; 4.3 moves the vendored runtime next to the executable.
        s_data.dotnet_root = fs::path(HN_DOTNET_RUNTIME_ROOT).lexically_normal();
#endif

        // Manifest: the project file, else --manifest, else the legacy game.yaml beside the asset root.
        if (!s_data.project_root.empty())
            ; // already set from the .honeyproj
        else if (auto cli = find_cli_manifest_arg(argc, argv))
            s_data.manifest_dir = normalized(*cli);
        else if (!s_data.asset_root.empty())
            s_data.manifest_dir = normalized(s_data.asset_root.parent_path() / "game.yaml");
        else if (mode == Mode::player) {
            HN_CORE_FATAL("[AssetPaths] Failure determining a valid location for the game manifest.");
            return false;
        }

        s_data.initialized = true;

        HN_CORE_INFO("AssetPaths: asset root = {} (rule: {})", s_data.asset_root.string(), asset_rule);
        HN_CORE_INFO("AssetPaths: engine content root = {} (rule: {})", s_data.engine_content_root.string(), ec_chosen->rule);
        HN_CORE_INFO("AssetPaths: config root = {}, managed layout = {}", s_data.config_root.string(),
                     s_data.dev_layout ? "dev" : "shipped");
        return true;
    }

    bool AssetPaths::open_project(const fs::path& project, std::string& error) {
        checked(s_data.asset_root);
        auto file = find_project_file(project, error);
        if (!file)
            return false;
        if (!apply_project(*file, error))
            return false;
        HN_CORE_INFO("AssetPaths: opened project {} (asset root = {})", s_data.project_root.string(), s_data.asset_root.string());
        return true;
    }

    void AssetPaths::close_project() {
        checked(s_data.asset_root);
        clear_project();
        HN_CORE_INFO("AssetPaths: project closed");
    }

    bool AssetPaths::has_asset_root() { checked(s_data.asset_root); return !s_data.asset_root.empty(); }

    const fs::path& AssetPaths::asset_root()                 { return checked(s_data.asset_root); }
    const fs::path& AssetPaths::engine_content_root()        { return checked(s_data.engine_content_root); }
    bool AssetPaths::has_project()                           { checked(s_data.asset_root); return !s_data.project_root.empty(); }
    const fs::path& AssetPaths::project_root()               { return checked(s_data.project_root); }
    const fs::path& AssetPaths::scripts_dir()                { return checked(s_data.scripts_dir); }
    const fs::path& AssetPaths::config_root()                { return checked(s_data.config_root); }
    const fs::path& AssetPaths::cache_dir()                  { return checked(s_data.cache_dir); }
    const fs::path& AssetPaths::user_data_dir()              { return checked(s_data.user_data_dir); }
    const fs::path& AssetPaths::manifest_path()              { return checked(s_data.manifest_dir); }
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
        checked(s_data.asset_root);
        fs::path root = s_data.asset_root;
        fs::path rel = asset_relative;
        if (const std::string g = asset_relative.generic_string(); is_engine_path(g)) {
            root = s_data.engine_content_root;
            rel = fs::path(g.substr(std::string_view(k_engine_prefix).size()));
        }
        if (root.empty()) {
            HN_CORE_WARN("AssetPaths::resolve: no project open, cannot resolve '{}'", asset_relative.generic_string());
            return {};
        }
        return resolve_under(root, rel, asset_relative);
    }

    fs::path AssetPaths::resolve_under(const fs::path& root, const fs::path& asset_relative, const fs::path& original) {
        if (asset_relative.empty())
            return {};
        if (asset_relative.is_absolute() || has_parent_segment(asset_relative)) {
            HN_CORE_WARN("AssetPaths::resolve: rejected non asset-relative path '{}'", original.generic_string());
            return {};
        }
        return (root / asset_relative).lexically_normal();
    }

    std::string AssetPaths::to_portable(const fs::path& path) {
        const fs::path& root = checked(s_data.asset_root);
        if (path.empty())
            return {};
        if (root.empty() && !is_engine_path(path.generic_string()) && !path.is_absolute())
            return path.generic_string(); // no project open: nothing to relativize against

        // Relative input is already in some stored form; round-trip it through from_portable so
        // legacy "../assets/x" is rewritten as "x".
        const fs::path absolute = path.is_absolute() ? path.lexically_normal() : from_portable(path.generic_string());

        if (is_engine_path(absolute.generic_string()))
            return absolute.generic_string();

        const fs::path ec_relative = absolute.lexically_relative(s_data.engine_content_root);
        if (!ec_relative.empty() && !has_parent_segment(ec_relative))
            return std::string(k_engine_prefix) + ec_relative.generic_string();

        const fs::path relative = root.empty() ? fs::path() : absolute.lexically_relative(root);
        if (relative.empty() || has_parent_segment(relative))
            return absolute.generic_string(); // outside both roots; keep as-is

        return relative.generic_string();
    }

    fs::path AssetPaths::from_portable(const std::string& stored) {
        const fs::path& root = checked(s_data.asset_root);
        if (stored.empty())
            return {};

        if (is_engine_path(stored))
            return resolve(stored);

        fs::path p(stored);
        if (p.is_absolute())
            return p.lexically_normal();
        if (root.empty())
            return {}; // no project open

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
