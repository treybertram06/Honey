#pragma once

#include <string>

namespace Honey {

    class FileDialogs {
    public:
        static std::string open_file(const char* filter);
        static std::string save_file(const char* filter);
    };

}