#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "frame_graph.h"

namespace Honey {

    class FrameGraphRegistry {
    public:
        static FrameGraphRegistry& get();

        bool register_executor(const std::string& id, FGPassExecutor executor);
        FGPassExecutor find_executor(const std::string& id) const;

        std::vector<std::string> list_executor_ids() const;

        void clear();

    private:
        std::unordered_map<std::string, FGPassExecutor> m_executors;
    };

}
