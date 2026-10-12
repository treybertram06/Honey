#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <spdlog/common.h>

#include "base.h"

namespace Honey {

    struct ConsoleEntry {
        uint64_t seq = 0;
        spdlog::level::level_enum level = spdlog::level::info;
        bool is_command = false; // echoed user input rather than a log line
        std::string logger;
        std::string function; // caller signature split off the HN_*_WARN/ERROR/FATAL prefix, empty if none
        std::string text;
        std::chrono::system_clock::time_point time;
    };

    // Thread-safe ring buffer of recent log output, fed by the spdlog ConsoleSink (log.cpp).
    // The console panel pulls new entries each frame with copy_since().
    class HONEY_API ConsoleBuffer {
    public:
        static constexpr size_t max_entries = 5000;

        static void push(ConsoleEntry entry);

        // Appends every entry with seq > last_seq to out. Returns the newest seq seen (or last_seq if none).
        static uint64_t copy_since(uint64_t last_seq, std::vector<ConsoleEntry>& out);

        static void clear();
    };

} // namespace Honey
