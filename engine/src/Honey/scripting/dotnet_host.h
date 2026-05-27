#pragma once
#include "vendor/dotnet/include/hostfxr.h"
#include <filesystem>
#include <string_view>

namespace Honey {

    class DotNetHost {
    public:
        bool init(const std::filesystem::path& runtime_config_path,
                  const std::filesystem::path& dotnet_root);
        void shutdown();

        // Returns a callable C fn ptr for the named managed method.
        void* load_managed_function(
            std::string_view assembly_path,
            std::string_view type_name,
            std::string_view method_name,
            std::string_view delegate_type_name = "");

    private:
        void* m_hostfxr_lib = nullptr;
        void* m_load_assembly_fn = nullptr;    // load_assembly_and_get_function_pointer_fn
        void* m_close_fn = nullptr;
        hostfxr_handle m_host_ctx = nullptr;
    };

}
