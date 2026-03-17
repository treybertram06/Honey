#pragma once

#include <filesystem>
#include <string_view>

#include "frame_graph.h"

namespace Honey {

    class FrameGraphLoader {
    public:
        // Loads and validates a graph description from disk.
        // Returns true if parsing/validation succeeded and out_desc is usable.
        static bool load_from_file(const std::filesystem::path& file_path,
                                   FGGraphDesc& out_desc,
                                   FGCompileDiagnostics& out_diagnostics);

        // Same as load_from_file, but parses YAML text directly.
        static bool load_from_string(std::string_view yaml_text,
                                     FGGraphDesc& out_desc,
                                     FGCompileDiagnostics& out_diagnostics,
                                     std::string_view debug_name = "<memory>");

        // Convenience: load YAML from disk and immediately compile it.
        // Returns nullptr if either loading/parsing/validation or compile fails.
        static std::shared_ptr<FrameGraphCompiled>
        load_and_compile_from_file(const std::filesystem::path& file_path,
                                   FGCompileDiagnostics& out_diagnostics,
                                   const FGCompileOptions* options = nullptr);

        // Helper to dump diagnostics in a grouped, readable form.
        static void log_diagnostics(const FGCompileDiagnostics& diagnostics,
                                    std::string_view context_label = "FrameGraph");
    };

}