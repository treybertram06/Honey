#pragma once

#include <functional>
#include <string_view>

#include "base.h"

namespace Honey {

    // Placeholder for the future console command system. The handler returns true if it consumed the command.
    class HONEY_API ConsoleCommands {
    public:
        using Handler = std::function<bool(std::string_view)>;

        static void set_handler(Handler handler);
        static void execute(std::string_view command);
    };

} // namespace Honey
