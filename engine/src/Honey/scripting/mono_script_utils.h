#pragma once
#include "mono_script_engine.h"

namespace Honey::Scripting {

    MonoObject* create_managed_entity(uint64_t entity_id) {
        MonoClass* entity_class = mono_class_from_name(MonoScriptEngine::get_image(), "Honey", "Entity");
        MonoObject* managed_entity = mono_object_new(MonoScriptEngine::get_domain(), entity_class);
        mono_runtime_object_init(managed_entity);

        // Set the ID field
        MonoClassField* id_field = mono_class_get_field_from_name(entity_class, "ID");
        if (id_field)
            mono_field_set_value(managed_entity, id_field, &entity_id);

        return managed_entity;
    }

}
