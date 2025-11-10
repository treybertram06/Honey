#include "components.h"
#include "scriptable_entity.h"
#include "script_registry.h"

namespace Honey {
    void NativeScriptComponent::destroy_script_impl(NativeScriptComponent* nsc) {
        delete nsc->instance;
        nsc->instance = nullptr;
    }

    void NativeScriptComponent::bind_by_name(const std::string& name) {
        script_name = name;
        instantiate_script = [name]() -> ScriptableEntity* {
            return ScriptRegistry::get().create_script(name);
        };
        destroy_script = destroy_script_impl;
    }
}
