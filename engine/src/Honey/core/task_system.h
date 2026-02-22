#pragma once

#include <TaskScheduler.h>
#include <functional>
#include <cstdint>

namespace Honey {

    struct TaskHandle {
        enki::ITaskSet* internal = nullptr;

        explicit operator bool() const noexcept { return internal != nullptr; }
    };

    class TaskSystem {
    public:
        static void init();
        static void shutdown();

        static enki::TaskScheduler& raw();

        static TaskHandle run_async(std::function<void()> fn);
        static void wait(TaskHandle handle);
        static void wait_for_all();

        static void enqueue_main(std::function<void()> fn);
        static void pump_main();

        template<typename Func>
        static TaskHandle parallel_for(uint32_t begin,
                                       uint32_t end,
                                       Func&& func,
                                       uint32_t minBatchSize = 64);

    private:
        static inline enki::TaskScheduler s_scheduler{};
        static inline bool s_initialized = false;

        static inline std::mutex s_main_mutex{};
        static inline std::vector<std::function<void()>> s_main_queue;
    };


    template<typename Func>
    TaskHandle TaskSystem::parallel_for(uint32_t begin,
                                        uint32_t end,
                                        Func&& func,
                                        uint32_t minBatchSize)
    {
        if (!s_initialized || begin >= end) {
            return {};
        }

        // Small adapter that forwards range indices [start, end) to a lambda
        struct LambdaTaskSet : enki::ITaskSet {
            Func        func;
            uint32_t    begin;
            uint32_t    end;

            LambdaTaskSet(Func&& f, uint32_t b, uint32_t e)
                : func(std::forward<Func>(f)), begin(b), end(e) {}

            void ExecuteRange(enki::TaskSetPartition range, uint32_t) override {
                // enkiTS gives us [start, end) in the same index space
                for (uint32_t i = range.start; i < range.end; ++i) {
                    func(i);
                }
            }
        };

        const uint32_t total = end - begin;
        if (total == 0)
            return {};

        // Choose a reasonable number of partitions based on minBatchSize
        const uint32_t batchSize = (minBatchSize == 0) ? 1u : minBatchSize;
        const uint32_t desiredPartitions = (total + batchSize - 1u) / batchSize;
        const uint32_t partitions = desiredPartitions > 0 ? desiredPartitions : 1u;

        // Allocate on the heap; TaskSystem::wait() will delete it.
        auto* task = new LambdaTaskSet(std::forward<Func>(func), begin, end);

        s_scheduler.AddTaskSetToPipe(task, partitions);
        return TaskHandle{ task };
    }

}