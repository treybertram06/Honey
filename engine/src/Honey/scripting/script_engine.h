#pragma once

extern "C" { // Because mono headers are massive, and written in C, it's being forward declared
    typedef struct _MonoDomain MonoDomain;
    typedef struct _MonoAssembly MonoAssembly;
    typedef struct _MonoImage MonoImage;
    typedef struct _MonoClass MonoClass;
    typedef struct _MonoObject MonoObject;
    typedef struct _MonoMethod MonoMethod;
}

namespace Honey {


    class ScriptClass {
    public:
        ScriptClass() = default;
        ScriptClass(const std::string& klass_namespace, const std::string& klass_name);

        MonoObject* instantiate();
        MonoMethod* get_method(const std::string& method_name, int param_count);
        MonoObject* invoke_method(MonoObject* instance, MonoMethod* method, void** params);

    private:
        std::string m_klass_namespace;
        std::string m_klass_name;
        MonoClass* m_klass = nullptr;
    };

    class ScriptEngine {
    public:
        static void init();
        static void shutdown();

    private:
        static void init_mono();
        static void shutdown_mono();
        static void load_assembly(const std::filesystem::path& path);
        static MonoObject* instantiate_class(MonoClass* klass);

        struct ScriptEngineData {
            MonoDomain* domain = nullptr;
            MonoAssembly* core_assembly = nullptr;
            MonoImage* core_image = nullptr;

            ScriptClass entity_class;
        };
        static std::unique_ptr<ScriptEngineData> s_data;

        friend class ScriptClass;
    };
}
