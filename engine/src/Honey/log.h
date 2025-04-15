#pragma once

#include <memory>

#include "core.h"
//#include <spdlog.h>
#include "../vendor/spdlog/include/spdlog.h"
#include <logger.h>
#include <sinks/stdout_color_sinks.h>

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

