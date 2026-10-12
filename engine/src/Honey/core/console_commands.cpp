#include "hnpch.h"
#include "console_commands.h"

#include "log.h"

namespace Honey {

    namespace {
        ConsoleCommands::Handler s_handler;
    }

    void ConsoleCommands::set_handler(Handler handler) {
        s_handler = std::move(handler);
    }

    void ConsoleCommands::execute(std::string_view command) {
        if (s_handler && s_handler(command))
            return;
        // Client logger so it still shows in release builds.
        HN_WARN("Unknown command: {}", command);
    }

} // namespace Honey
