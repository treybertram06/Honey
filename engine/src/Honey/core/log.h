#pragma once

#include "base.h"


//#include "vendor/spdlog/include/spdlog/spdlog.h"
#include <spdlog/spdlog.h>
//#include "vendor/spdlog/include/spdlog/logger.h"
#include <spdlog/logger.h>
//#include "vendor/spdlog/include/spdlog/sinks/stdout_color_sinks.h"
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Honey {

    class HONEY_API Log {

    public:
        static void init();

        inline static std::shared_ptr<spdlog::logger>& get_core_logger() { return s_core_logger; }
        inline static std::shared_ptr<spdlog::logger>& get_client_logger() { return s_client_logger; }

    private:
        static std::shared_ptr<spdlog::logger> s_core_logger;
        static std::shared_ptr<spdlog::logger> s_client_logger;
    };


}

//core log macros
#ifdef BUILD_DEBUG
#define HN_CORE_FATAL(...)   ::Honey::Log::get_core_logger()->fatal("[{}] {}", CURRENT_FUNCTION, fmt::format(__VA_ARGS__))
#define HN_CORE_ERROR(...)   ::Honey::Log::get_core_logger()->error("[{}] {}", CURRENT_FUNCTION, fmt::format(__VA_ARGS__))
#define HN_CORE_WARN(...)    ::Honey::Log::get_core_logger()->warn ("[{}] {}", CURRENT_FUNCTION, fmt::format(__VA_ARGS__))
#define HN_CORE_INFO(...)    ::Honey::Log::get_core_logger()->info ("[{}] {}", CURRENT_FUNCTION, fmt::format(__VA_ARGS__))
#define HN_CORE_TRACE(...)   ::Honey::Log::get_core_logger()->trace("[{}] {}", CURRENT_FUNCTION, fmt::format(__VA_ARGS__))
#else
#define HN_CORE_FATAL(...)   (void)0
#define HN_CORE_ERROR(...)   (void)0
#define HN_CORE_WARN(...)    (void)0
#define HN_CORE_INFO(...)    (void)0
#define HN_CORE_TRACE(...)   (void)0
#endif

// client log macros
#define HN_FATAL(...)        ::Honey::Log::get_client_logger()->fatal("[{}] {}", CURRENT_FUNCTION, fmt::format(__VA_ARGS__))
#define HN_ERROR(...)        ::Honey::Log::get_client_logger()->error("[{}] {}", CURRENT_FUNCTION, fmt::format(__VA_ARGS__))
#define HN_WARN(...)         ::Honey::Log::get_client_logger()->warn ("[{}] {}", CURRENT_FUNCTION, fmt::format(__VA_ARGS__))
#define HN_INFO(...)         ::Honey::Log::get_client_logger()->info ("[{}] {}", CURRENT_FUNCTION, fmt::format(__VA_ARGS__))
#define HN_TRACE(...)        ::Honey::Log::get_client_logger()->trace("[{}] {}", CURRENT_FUNCTION, fmt::format(__VA_ARGS__))