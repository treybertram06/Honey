#pragma once

#include <cstdint>
#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemWithBarrier.h>

#include "TaskScheduler.h"
#include "Jolt/Core/FixedSizeFreeList.h"

namespace Honey {

    class JoltJobSystem final : public JPH::JobSystemWithBarrier {
    public:
        static constexpr uint32_t cMaxJobs = 2048;
        static constexpr uint32_t cMaxBarriers = 64;

        JoltJobSystem();
        ~JoltJobSystem() override;

        int GetMaxConcurrency() const override;

        void flush();

        JobHandle CreateJob(const char* name, JPH::ColorArg color,
            const JobFunction& fn, JPH::uint32 deps) override;
    protected:
        void FreeJob(JPH::JobSystem::Job* job) override;
        void QueueJob(JPH::JobSystem::Job* job) override;
        void QueueJobs(JPH::JobSystem::Job** jobs, uint32_t count) override;

    private:
        JPH::FixedSizeFreeList<Job> m_jobs;

        std::mutex m_tasks_mutex;
        std::vector<enki::ITaskSet*> m_pending_tasks;
    };

}
