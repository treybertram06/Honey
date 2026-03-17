#include "hnpch.h"
#include "frame_graph_registry.h"

namespace Honey {

    FrameGraphRegistry& FrameGraphRegistry::get() {
        static FrameGraphRegistry s_instance;
        return s_instance;
    }

    bool FrameGraphRegistry::register_executor(const std::string& id, FGPassExecutor executor) {
        if (id.empty() || !executor)
            return false;

        m_executors[id] = std::move(executor);
        return true;
    }

    FGPassExecutor FrameGraphRegistry::find_executor(const std::string& id) const {
        const auto it = m_executors.find(id);
        if (it == m_executors.end())
            return {};
        return it->second;
    }

    std::vector<std::string> FrameGraphRegistry::list_executor_ids() const {
        std::vector<std::string> ids;
        ids.reserve(m_executors.size());
        for (const auto& [id, _] : m_executors) {
            ids.push_back(id);
        }
        return ids;
    }

    void FrameGraphRegistry::clear() {
        m_executors.clear();
    }

}
