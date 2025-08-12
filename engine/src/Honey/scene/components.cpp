#include "components.h"
#include "scriptable_entity.h"

namespace Honey {
    void NativeScriptComponent::destroy_script_impl(NativeScriptComponent* nsc) {
        delete nsc->instance;
        nsc->instance = nullptr;
    }
}
