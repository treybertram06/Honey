#include "hnpch.h"
#include "script_glue.h"

#include <mono/metadata/loader.h>
#include <mono/metadata/object.h>
#include "glm/vec3.hpp"

namespace Honey {

#define HN_ADD_INTERNAL_CALL(name) mono_add_internal_call("Honey.InternalCalls::"#name, (const void*)name)

    static void native_log(MonoString* message) {
        char* raw_str = mono_string_to_utf8(message);
        std::string mono_string(raw_str);
        mono_free(raw_str);

        HN_INFO("{}", mono_string);
    }

    static void native_log_vec(glm::vec3* vec) {
        HN_INFO("{}, {}, {}", vec->x, vec->y, vec->z);
    }

    void ScriptGlue::register_functions() {
        HN_ADD_INTERNAL_CALL(native_log);
        HN_ADD_INTERNAL_CALL(native_log_vec);
    }

}
