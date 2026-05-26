#pragma once
#include "dotnet_host.h"
#include "Honey/scene/scene.h"
#include <string_view>

namespace Honey {
    class CSharpScriptGlue {
    public:
        // Builds NativeFunctionTable, gets Bootstrap fn ptr from HoneyEngine.dll, calls it.
        // Must be called after DotNetHost::init().
        static void register_functions(DotNetHost& host, std::string_view honey_engine_dll_path);

        // Called by CSharpScriptEngine on runtime start/stop so glue can resolve entities.
        static void set_scene_context(Scene* scene);

        static Scene* s_scene_context;
    };
}