#include "hnpch.h"
#include "script_engine.h"

#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>

#include "script_glue.h"
#include "glm/vec3.hpp"

namespace Honey {

    namespace utils {

        static char* read_bytes(const std::string& filepath, uint32_t* out_size) {
            std::ifstream stream(filepath, std::ios::binary | std::ios::ate);

            if (!stream) {
                // Failed to open the file
                return nullptr;
            }

            std::streampos end = stream.tellg();
            stream.seekg(0, std::ios::beg);
            uint32_t size = end - stream.tellg();

            if (size == 0) {
                // File is empty
                return nullptr;
            }

            char* buffer = new char[size];
            stream.read((char*)buffer, size);
            stream.close();

            *out_size = size;
            return buffer;
        }

        static MonoAssembly *load_mono_assembly(const std::filesystem::path& assembly_path) {
            uint32_t file_size = 0;
            char* file_data = read_bytes(assembly_path.generic_string(), &file_size);

            // NOTE: We can't use this image for anything other than loading the assembly because this image doesn't have a reference to the assembly
            MonoImageOpenStatus status;
            MonoImage* image = mono_image_open_from_data_full(file_data, file_size, 1, &status, 0);

            if (status != MONO_IMAGE_OK) {
                const char* error_message = mono_image_strerror(status);
                HN_CORE_ERROR("Failed to load assembly '{0}': {1}", assembly_path.string(), error_message);
                return nullptr;
            }

            std::string path_string = assembly_path.string();
            MonoAssembly* assembly = mono_assembly_load_from_full(image, path_string.c_str(), &status, 0);
            mono_image_close(image);

            // Don't forget to free the file data
            delete[] file_data;

            return assembly;
        }

        void print_assembly_types(MonoAssembly* assembly) {
            MonoImage* image = mono_assembly_get_image(assembly);
            const MonoTableInfo* type_definitions_table = mono_image_get_table_info(image, MONO_TABLE_TYPEDEF);
            int32_t num_types = mono_table_info_get_rows(type_definitions_table);

            for (int32_t i = 0; i < num_types; i++) {
                uint32_t cols[MONO_TYPEDEF_SIZE];
                mono_metadata_decode_row(type_definitions_table, i, cols, MONO_TYPEDEF_SIZE);

                const char* name_space = mono_metadata_string_heap(image, cols[MONO_TYPEDEF_NAMESPACE]);
                const char* name = mono_metadata_string_heap(image, cols[MONO_TYPEDEF_NAME]);

                HN_CORE_TRACE("{}.{}", name_space, name);
            }
        }

        static std::filesystem::path get_mono_root() {
#ifdef HN_MONO_ROOT
            return std::filesystem::path(HN_MONO_ROOT);
#else
            #error "HN_MONO_ROOT must be defined (cause how else should it know where the hell the mono libraries live???)"
#endif
        }

        static void configure_mono_dirs()
        {
            const auto root = get_mono_root();

            const auto lib_dir = (root / "lib").string();
            const auto etc_dir = (root / "etc").string();
            const auto assemblies_dir = (root / "lib" / "mono").string();

            mono_set_dirs(lib_dir.c_str(), etc_dir.c_str());
            mono_set_assemblies_path(assemblies_dir.c_str());
        }

    }

    std::unique_ptr<ScriptEngine::ScriptEngineData> ScriptEngine::s_data = nullptr;

    ScriptClass::ScriptClass(const std::string& klass_namespace, const std::string& klass_name)
    : m_klass_namespace(klass_namespace), m_klass_name(klass_name) {
        m_klass = mono_class_from_name(ScriptEngine::s_data->core_image, m_klass_namespace.c_str(), m_klass_name.c_str());
    }

    MonoObject* ScriptClass::instantiate() {
        return ScriptEngine::instantiate_class(m_klass);
    }

    MonoMethod* ScriptClass::get_method(const std::string& method_name, int param_count) {
        return mono_class_get_method_from_name(m_klass, method_name.c_str(), param_count);
    }

    MonoObject* ScriptClass::invoke_method(MonoObject* instance, MonoMethod* method, void** params = nullptr) {
        return mono_runtime_invoke(method, instance, params, nullptr);
    }

    ScriptInstance::ScriptInstance(Ref<ScriptClass> script_class)
        : m_script_class(script_class) {
        m_instance = m_script_class->instantiate();

        m_on_create_method = m_script_class->get_method("OnCreate", 0);
        m_on_update_method = m_script_class->get_method("OnUpdate", 1);
    }

    void ScriptInstance::invoke_on_create() {
        m_script_class->invoke_method(m_instance, m_on_create_method);
    }

    void ScriptInstance::invoke_on_update(float ts) {
        void* params[1];
        params[0] = &ts;
        m_script_class->invoke_method(m_instance, m_on_update_method, params);
    }


    void ScriptEngine::init() {
        s_data = std::make_unique<ScriptEngineData>();

        init_mono();

        load_assembly("../assets/scripts/scripts/Honey-ScriptCore.dll");
        load_assembly_classes(s_data->core_assembly);
        ScriptGlue::register_functions();
    }

    void ScriptEngine::shutdown() {
        shutdown_mono();
    }


    void ScriptEngine::init_mono() {
        utils::configure_mono_dirs();

        // mono_config_parse(nullptr);

        MonoDomain* domain = mono_jit_init("HoneyJitRuntime");
        HN_CORE_ASSERT(domain, "Failed to initialize Mono JIT runtime!");
        s_data->domain = domain;
    }


    void ScriptEngine::shutdown_mono() {
        mono_jit_cleanup(s_data->domain);
    }

    void ScriptEngine::load_assembly(const std::filesystem::path& path) {
        s_data->core_assembly = utils::load_mono_assembly(path);
        HN_CORE_ASSERT(s_data->core_assembly, "Failed to load assembly!");
        s_data->core_image = mono_assembly_get_image(s_data->core_assembly);
        HN_CORE_ASSERT(s_data->core_image, "Failed to get MonoImage from assembly!");
        //print_assembly_types(s_data->core_assembly);
    }

    void ScriptEngine::load_assembly_classes(MonoAssembly* assembly) {
        s_data->entity_classes.clear();

        MonoImage* image = mono_assembly_get_image(assembly);
        const MonoTableInfo* type_definitions_table = mono_image_get_table_info(image, MONO_TABLE_TYPEDEF);
        int32_t num_types = mono_table_info_get_rows(type_definitions_table);
        MonoClass* entity_class = mono_class_from_name(image, "Honey", "Entity");

        for (int32_t i = 0; i < num_types; i++) {
            uint32_t cols[MONO_TYPEDEF_SIZE];
            mono_metadata_decode_row(type_definitions_table, i, cols, MONO_TYPEDEF_SIZE);

            const char* name_space = mono_metadata_string_heap(image, cols[MONO_TYPEDEF_NAMESPACE]);
            const char* name = mono_metadata_string_heap(image, cols[MONO_TYPEDEF_NAME]);
            std::string full_name;
            if (strlen(name_space) != 0)
                full_name = fmt::format("{}.{}", name_space, name);
            else
                full_name = name;

            MonoClass* mono_class = mono_class_from_name(image, name_space, name);
            if (mono_class == entity_class) continue;
            bool is_subclass = mono_class_is_subclass_of(mono_class, entity_class, false);
            if (is_subclass)
                s_data->entity_classes[full_name] = CreateRef<ScriptClass>(name_space, name);
        }
    }

    MonoObject* ScriptEngine::instantiate_class(MonoClass *klass) {
        MonoObject* instance = mono_object_new(s_data->domain, klass);
        HN_CORE_ASSERT(instance, "Failed to create MonoObject!");
        mono_runtime_object_init(instance);
        return instance;
    }


}
