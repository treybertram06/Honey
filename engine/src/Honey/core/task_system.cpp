#include "hnpch.h"
#include "task_system.h"

namespace Honey {

    namespace {
        struct FunctionTaskSet final : enki::ITaskSet {
            std::function<void()> fn;

            explicit FunctionTaskSet(std::function<void()> f)
                : fn(std::move(f)) {}

            void ExecuteRange(enki::TaskSetPartition, uint32_t) override {
                if (fn) {
                    fn();
                }
            }
        };
    }

    void TaskSystem::init() {
        if (s_initialized)
            return;

        enki::TaskSchedulerConfig cfg{};
        uint32_t hw = enki::GetNumHardwareThreads();
        cfg.numTaskThreadsToCreate = hw > 1 ? (hw - 1) : 1;


        s_scheduler.Initialize(cfg);
        s_initialized = true;

        HN_CORE_INFO("TaskSystem initialized with {} worker threads",
                     s_scheduler.GetNumTaskThreads());
    }

    void TaskSystem::shutdown() {
        if (!s_initialized)
            return;

        // Ensure all outstanding work is complete before shutting down.
        s_scheduler.WaitforAllAndShutdown();
        s_initialized = false;
        
        // Clear any queued main-thread work.
        {
            std::lock_guard<std::mutex> lock(s_main_mutex);
            s_main_queue.clear();
        }

        HN_CORE_INFO("TaskSystem shutdown complete");
    }

    enki::TaskScheduler& TaskSystem::raw() {
        HN_CORE_ASSERT(s_initialized, "TaskSystem::raw() called before init()");
        return s_scheduler;
    }

    TaskHandle TaskSystem::run_async(std::function<void()> fn) {
        if (!s_initialized || !fn) {
            return {};
        }

        auto* task = new FunctionTaskSet(std::move(fn));
        // Single partition – just run fn() once on some worker
        s_scheduler.AddTaskSetToPipe(task);
        return TaskHandle{ task };
    }

    void TaskSystem::wait(TaskHandle handle) {
        if (!s_initialized || !handle.internal)
            return;

        s_scheduler.WaitforTask(handle.internal);
        delete handle.internal;
        handle.internal = nullptr;
    }

    void TaskSystem::wait_for_all() {
        if (!s_initialized)
            return;

        s_scheduler.WaitforAll();
    }
    
    void TaskSystem::enqueue_main(std::function<void()> fn) {
        if (!s_initialized || !fn)
            return;

        std::lock_guard<std::mutex> lock(s_main_mutex);
        s_main_queue.emplace_back(std::move(fn));
    }

    void TaskSystem::pump_main() {
        if (!s_initialized)
            return;

        std::vector<std::function<void()>> local;
        {
            std::lock_guard<std::mutex> lock(s_main_mutex);
            local.swap(s_main_queue);
        }

        for (auto& fn : local) {
            if (fn) fn();
        }
    }

}