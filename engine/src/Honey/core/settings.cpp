#include "hnpch.h"
#include "settings.h"
#include "Honey/math/yaml_glm.h"

#include <yaml-cpp/yaml.h>

namespace Honey {

    template <typename E> struct EnumNames;
    template <> struct EnumNames<GeometryPath> {
        static constexpr std::pair<GeometryPath, const char*> values[] = {
            { GeometryPath::Meshlet, "Meshlet" },
            { GeometryPath::Classic, "Classic" },
        };
    };

    template <> struct EnumNames<RendererAPI::API> {
        static constexpr std::pair<RendererAPI::API, const char*> values[] = {
            { RendererAPI::API::none, "None" },
            { RendererAPI::API::opengl, "OpenGL" },
            { RendererAPI::API::vulkan, "Vulkan" },
        };
    };

    template <> struct EnumNames<RendererSettings::RendererType> {
        static constexpr std::pair<RendererSettings::RendererType, const char*> values[] = {
            { RendererSettings::RendererType::deferred, "Deferred" },
            { RendererSettings::RendererType::forward, "Forward" },
            { RendererSettings::RendererType::pathtracing, "Pathtracing" },
            { RendererSettings::RendererType::lite, "Lite" },
        };
    };

    template <> struct EnumNames<RendererSettings::TextureFilter> {
        static constexpr std::pair<RendererSettings::TextureFilter, const char*> values[] = {
            { RendererSettings::TextureFilter::nearest, "Nearest" },
            { RendererSettings::TextureFilter::linear, "Linear" },
            { RendererSettings::TextureFilter::anisotropic, "Anisotropic" },
        };
    };

    template <> struct EnumNames<CullMode> {
        static constexpr std::pair<CullMode, const char*> values[] = {
            { CullMode::None, "None" },
            { CullMode::Back, "Back" },
            { CullMode::Front, "Front" },
        };
    };

    using AntiAliasingType = RendererSettings::AntiAliasingSettings::AAType;
    template <> struct EnumNames<AntiAliasingType> {
        static constexpr std::pair<AntiAliasingType, const char*> values[] = {
            { AntiAliasingType::none, "None" },
            { AntiAliasingType::fxaa, "FXAA" },
            { AntiAliasingType::taa, "TAA" },
        };
    };

    template <typename E>
    concept HasEnumNames = requires { EnumNames<E>::values; };

    namespace detail {
        inline bool iequals(std::string_view a, std::string_view b) {
            return std::ranges::equal(a, b, [](unsigned char x, unsigned char y) {
                return std::tolower(x) == std::tolower(y);
            });
        }
    }
}

namespace YAML {
    // One converter for every enum that has an EnumNames table
    template <Honey::HasEnumNames E>
    struct convert<E> {
        static Node encode(E value) {
            for (auto [v, name] : Honey::EnumNames<E>::values)
                if (v == value) return Node(name);
            return Node(Honey::EnumNames<E>::values[0].second);
        }
        static bool decode(const Node& node, E& out) {
            if (!node.IsScalar()) return false;
            const std::string& s = node.Scalar();
            for (auto [v, name] : Honey::EnumNames<E>::values)
                if (Honey::detail::iequals(s, name)) { out = v; return true; }
            return false; // Unknown name: caller warns and keeps current value
        }
    };
}

namespace Honey {

    namespace {
        // Missing file = empty map (not an error). Parse error = false (caller must NOT overwrite the file).
        bool read_yaml_file(const std::filesystem::path& path, YAML::Node& out) {
            if (!std::filesystem::exists(path)) { out = YAML::Node(YAML::NodeType::Map); return true; }
            try {
                out = YAML::LoadFile(path.string());
            } catch (const YAML::Exception& e) {
                HN_CORE_ERROR("Failed to parse {}: {}", path.string(), e.what());
                return false;
            }
            if (!out.IsMap()) out = YAML::Node(YAML::NodeType::Map);   // empty file loads as Null
            return true;
        }

        // Write to <path>.tmp, then rename over the original: a crash mid-write never truncates the real file.
        bool write_yaml_file_atomic(const std::filesystem::path& path, const YAML::Node& root) {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);

            auto tmp = path;
            tmp += ".tmp";
            {
                std::ofstream stream(tmp, std::ios::trunc);
                if (!stream) { HN_CORE_ERROR("Can't write {}", tmp.string()); return false; }
                YAML::Emitter out;
                out << root;
                stream << out.c_str() << '\n';
                if (!stream.flush()) { HN_CORE_ERROR("Write failed for {}", tmp.string()); return false; }
            }
            std::filesystem::rename(tmp, path, ec);   // replaces atomically on POSIX; replace-existing on Windows
            if (ec) { HN_CORE_ERROR("Can't replace {}: {}", path.string(), ec.message()); return false; }
            return true;
        }

        struct SettingDesc {
            SettingOwner owner;
            const char* section; // ex: "Renderer"
            const char* key;     // ex: "DirShadowDistance"
            std::function<void(const YAML::Node&, EngineSettings&)>             read;
            std::function<YAML::Node(const EngineSettings&)>                    write;
            std::function<void(EngineSettings&, const EngineSettings&)>         copy;   // dst, src
            std::function<bool(const EngineSettings&, const EngineSettings&)>   equal;
        };

        template <typename T>
        SettingDesc field(SettingOwner owner, const char* section, const char* key,
                        T& (*access)(EngineSettings&),
                        T (*sanitize)(T) = nullptr) {
            // `access` is non-const so one accessor serves read and write; the const_casts below only read.
            auto cget = [access](const EngineSettings& s) -> const T& {
                return access(const_cast<EngineSettings&>(s));
            };

            SettingDesc d{ owner, section, key };
            d.read = [=](const YAML::Node& node, EngineSettings& s) {
                T value;
                try {
                    value = node.as<T>();
                } catch (const YAML::Exception& e) {
                    HN_CORE_WARN("Settings: bad value for {}.{} ({}), keeping {}", section, key, e.what(),
                                 "current value");
                    return;
                }
                access(s) = sanitize ? sanitize(value) : value;
            };
            d.write = [=](const EngineSettings& s) { return YAML::Node(cget(s)); };
            d.copy  = [=](EngineSettings& dst, const EngineSettings& src) { access(dst) = cget(src); };
            d.equal = [=](const EngineSettings& a, const EngineSettings& b) { return cget(a) == cget(b); };
            return d;
        }
    }

    namespace {
        // Row with a hand-written codec: the on-disk Fullscreen bool maps onto WindowMode.
        SettingDesc legacy_fullscreen_row() {
            SettingDesc d{ SettingOwner::user, "Window", "Fullscreen" };
            // Legacy bool: the editor persists maximized-or-not. Real fullscreen is manifest-only.
            d.read = [](const YAML::Node& n, EngineSettings& s) {
                try {
                    s.window.mode = n.as<bool>() ? WindowMode::maximized : WindowMode::windowed;
                } catch (const YAML::Exception& e) {
                    HN_CORE_WARN("Settings: bad value for Window.Fullscreen ({}), keeping current value", e.what());
                }
            };
            d.write = [](const EngineSettings& s) { return YAML::Node(s.window.mode == WindowMode::maximized); };
            d.copy  = [](EngineSettings& dst, const EngineSettings& src) { dst.window.mode = src.window.mode; };
            d.equal = [](const EngineSettings& a, const EngineSettings& b) { return a.window.mode == b.window.mode; };
            return d;
        }

        void validate_schema(const std::vector<SettingDesc>& rows) {
            for (size_t i = 0; i < rows.size(); ++i)
                for (size_t j = i + 1; j < rows.size(); ++j)
                    HN_CORE_ASSERT(std::string_view(rows[i].section) != rows[j].section ||
                                   std::string_view(rows[i].key) != rows[j].key,
                                   "Duplicate settings row {}.{}", rows[i].section, rows[i].key);
        }

        // Clamped to the same ranges as the renderer debug panel sliders (FXAA 3.11's documented limits),
        // so a hand-edited file can't bypass them.
        float clamp_aa_subpix(float v)   { return std::clamp(v, 0.0f, 1.0f); }
        float clamp_aa_edge(float v)     { return std::clamp(v, 0.063f, 0.333f); }
        float clamp_aa_edge_min(float v) { return std::clamp(v, 0.0312f, 0.0833f); }
        int   clamp_substeps(int v)      { return std::max(v, 1); }

// Deduces T from the member expression so rows only name the member once.
#define HN_SETTING(owner, section, key, member, ...)                                         \
    field<std::remove_cvref_t<decltype(std::declval<EngineSettings&>().member)>>(           \
        SettingOwner::owner, section, key,                                                  \
        [](EngineSettings& s) -> auto& { return s.member; } __VA_OPT__(,) __VA_ARGS__)

        // The single source of truth for which persisted setting lives in which file.
        // Every persisted setting has exactly one row; fields without a row are never serialized
        // (e.g. anti_aliasing.debug_view, physics.show_jolt_debug_draw).
        const std::vector<SettingDesc>& schema() {
            static const std::vector<SettingDesc> s_rows = [] {
                std::vector<SettingDesc> rows = {
                    // ---- Renderer: per user (hardware / debug / editor) ----
                    HN_SETTING(user, "Renderer", "API",                       renderer.api),
                    HN_SETTING(user, "Renderer", "ClearColor",                renderer.clear_color),
                    HN_SETTING(user, "Renderer", "Wireframe",                 renderer.wireframe),
                    HN_SETTING(user, "Renderer", "DepthTest",                 renderer.depth_test),
                    HN_SETTING(user, "Renderer", "DepthWrite",                renderer.depth_write),
                    HN_SETTING(user, "Renderer", "FaceCulling",               renderer.face_culling),
                    HN_SETTING(user, "Renderer", "Blending",                  renderer.blending),
                    HN_SETTING(user, "Renderer", "VSync",                     renderer.vsync),
                    HN_SETTING(user, "Renderer", "ShowPhysicsDebugDraw",      renderer.show_physics_debug_draw),
                    HN_SETTING(user, "Renderer", "ParallelMeshSubmission",    renderer.enable_parallel_mesh_submission),
                    HN_SETTING(user, "Renderer", "RendererType",              renderer.renderer_type),
                    HN_SETTING(user, "Renderer", "AnisotropicFilteringLevel", renderer.anisotropic_filtering_level),
                    HN_SETTING(user, "Renderer", "TextureFilter",             renderer.texture_filter),
                    HN_SETTING(user, "Renderer", "CullMode",                  renderer.cull_mode),
                    HN_SETTING(user, "Renderer", "EditorCameraExposure",      renderer.editor_camera_exposure),

                    // ---- Renderer: per project (look) ----
                    HN_SETTING(project, "Renderer", "GeometryPath",       renderer.geometry_path),
                    HN_SETTING(project, "Renderer", "DirShadowDistance",  renderer.dir_shadow_distance),
                    HN_SETTING(project, "Renderer", "IBLIntensity",       renderer.ibl_intensity),
                    HN_SETTING(project, "Renderer", "BloomStrength",      renderer.bloom.strength),
                    HN_SETTING(project, "Renderer", "BloomThreshold",     renderer.bloom.threshold),
                    HN_SETTING(project, "Renderer", "BloomSoftKnee",      renderer.bloom.soft_knee),
                    HN_SETTING(project, "Renderer", "AASubpix",           renderer.anti_aliasing.subpix,             &clamp_aa_subpix),
                    HN_SETTING(project, "Renderer", "AAEdgeThreshold",    renderer.anti_aliasing.edge_threshold,     &clamp_aa_edge),
                    HN_SETTING(project, "Renderer", "AAEdgeThresholdMin", renderer.anti_aliasing.edge_threshold_min, &clamp_aa_edge_min),
                    HN_SETTING(project, "Renderer", "AAType",             renderer.anti_aliasing.type),

                    // ---- Physics: per project ----
                    HN_SETTING(project, "Physics", "Enabled",  physics.enabled),
                    HN_SETTING(project, "Physics", "Substeps", physics.substeps, &clamp_substeps),

                    // ---- Window / Editor: per user ----
                    HN_SETTING(user, "Window", "Title",  window.title),
                    HN_SETTING(user, "Window", "Width",  window.width),
                    HN_SETTING(user, "Window", "Height", window.height),
                    HN_SETTING(user, "Window", "PosX",   window.pos_x),
                    HN_SETTING(user, "Window", "PosY",   window.pos_y),
                    HN_SETTING(user, "Editor", "GizmoIconSize", editor.gizmo_icon_size),
                };
                rows.push_back(legacy_fullscreen_row());
                validate_schema(rows);
                return rows;
            }();
            return s_rows;
        }
#undef HN_SETTING

        // ---- Generic, owner-filtered passes over the schema ----

        void read_rows(const YAML::Node& root, EngineSettings& s, SettingOwner owner) {
            for (const auto& d : schema()) {
                if (d.owner != owner) continue;
                const YAML::Node section = root[d.section];   // const Node: lookup never inserts
                if (!section || !section.IsMap()) continue;
                if (const YAML::Node n = section[d.key]) d.read(n, s);
            }
        }

        // YAML::Node has reference semantics: always write through the full chain, never via a copied section node.
        void write_rows(YAML::Node& root, const EngineSettings& s, SettingOwner owner) {
            for (const auto& d : schema())
                if (d.owner == owner) root[d.section][d.key] = d.write(s);
        }

        void copy_rows(EngineSettings& dst, const EngineSettings& src, SettingOwner owner) {
            for (const auto& d : schema())
                if (d.owner == owner) d.copy(dst, src);
        }

        bool rows_equal(const EngineSettings& a, const EngineSettings& b, SettingOwner owner) {
            return std::ranges::all_of(schema(), [&](const SettingDesc& d) {
                return d.owner != owner || d.equal(a, b);
            });
        }

        // A project trying to set a per-user key (e.g. RendererType: Lite) is ignored, but loudly.
        void warn_on_user_keys(const YAML::Node& block) {
            for (const auto& d : schema()) {
                if (d.owner != SettingOwner::user) continue;
                const YAML::Node section = block[d.section];
                if (section && section.IsMap() && section[d.key])
                    HN_CORE_WARN("Project settings: {}.{} is a per-user setting and is ignored here", d.section, d.key);
            }
        }

        // Project rows as of the last load/reset/save; project_dirty() compares against this.
        EngineSettings s_project_snapshot;
    }

    // ---------------- User layer ----------------

    bool Settings::load_user(const std::filesystem::path& path) {
        HN_PROFILE_FUNCTION();

        YAML::Node root;
        if (!read_yaml_file(path, root))
            return false;

        read_rows(root, get(), SettingOwner::user); // Legacy project-owned keys in this file are ignored
        HN_CORE_INFO("Loaded user settings from {}", path.string());
        return true;
    }

    bool Settings::save_user(const std::filesystem::path& path) {
        HN_PROFILE_FUNCTION();

        YAML::Node root;
        if (!read_yaml_file(path, root)) {
            HN_CORE_ERROR("Not overwriting unparseable settings file {}", path.string());
            return false;
        }

        // Start from the existing file so unknown keys survive, but drop project-owned keys left over from
        // before the split: an ignored key that still sits in the file just invites editing the wrong file.
        for (const auto& d : schema()) {
            if (d.owner != SettingOwner::project) continue;
            YAML::Node section = root[d.section];
            if (section && section.IsMap())
                section.remove(d.key);
        }

        write_rows(root, get(), SettingOwner::user);
        if (!write_yaml_file_atomic(path, root))
            return false;

        HN_CORE_INFO("Saved user settings to {}", path.string());
        return true;
    }

    // ---------------- Project layer ----------------

    void Settings::load_project(const YAML::Node& block) {
        HN_PROFILE_FUNCTION();

        // Stage on a copy so a project's values fully replace the previous project's, starting from engine defaults.
        EngineSettings staged = get();
        copy_rows(staged, EngineSettings{}, SettingOwner::project);
        if (block && block.IsMap()) {
            warn_on_user_keys(block);
            read_rows(block, staged, SettingOwner::project);
        } else if (block && !block.IsNull()) {
            HN_CORE_WARN("Project 'Settings' block is not a map; using engine defaults");
        }

        copy_rows(get(), staged, SettingOwner::project);
        s_project_snapshot = get();
    }

    void Settings::reset_project() {
        copy_rows(get(), EngineSettings{}, SettingOwner::project);
        s_project_snapshot = get();
    }

    bool Settings::save_project(const std::filesystem::path& honeyproj) {
        HN_PROFILE_FUNCTION();

        YAML::Node root;
        if (!read_yaml_file(honeyproj, root)) {
            HN_CORE_ERROR("Not overwriting unparseable project file {}", honeyproj.string());
            return false;
        }

        // Every project row is written (not just non-defaults): a saved project is fully explicit, so a later
        // change to engine defaults can't silently change how an existing game looks.
        YAML::Node block(YAML::NodeType::Map);
        write_rows(block, get(), SettingOwner::project);
        root["Settings"] = block; // Replaces only this subtree; Game/Window/Project are untouched

        if (!write_yaml_file_atomic(honeyproj, root))
            return false;

        s_project_snapshot = get();
        HN_CORE_INFO("Saved project settings to {}", honeyproj.string());
        return true;
    }

    bool Settings::project_dirty() {
        return !rows_equal(get(), s_project_snapshot, SettingOwner::project);
    }

    // ---------------- Single-key writer ----------------

    // The API switch needs a restart, so it writes just this one key instead of every pending user tweak.
    bool Settings::write_renderer_api_to_file(const std::filesystem::path& path) {
        YAML::Node root;
        if (!read_yaml_file(path, root)) {
            HN_CORE_ERROR("Not overwriting unparseable settings file {}", path.string());
            return false;
        }

        root["Renderer"]["API"] = YAML::Node(get().renderer.api);
        if (!write_yaml_file_atomic(path, root))
            return false;

        HN_CORE_INFO("Updated Renderer.API in settings file: {}", path.string());
        return true;
    }
}
