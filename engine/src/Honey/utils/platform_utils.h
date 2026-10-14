#pragma once

#include <filesystem>
#include <string>

namespace Honey {

    class FileDialogs {
    public:
        static std::string open_file(const char* filter);
        static std::string save_file(const char* filter);
    };

    class Platform {
    public:
        // Absolute path of the running executable (symlinks resolved). Empty on failure.
        // Do not use argv[0] for this: it is cwd-relative or a bare name when launched from PATH.
        static std::filesystem::path get_executable_path();
    };

}