#include "hnpch.h"
#include "jolt_job_system.h"

#include "Honey/core/task_system.h"

namespace Honey {
    JoltJobSystem::JoltJobSystem() {
        JobSystemWithBarrier::Init(cMaxBarriers); // Init barrier pool
        m_jobs.Init(cMaxJobs, cMaxJobs); // Init the job memory pool
    }

    JoltJobSystem::~JoltJobSystem() {
        flush();
    }

    int JoltJobSystem::GetMaxConcurrency() const {
        return (int)TaskSystem::raw().GetNumTaskThreads();
    }

    void JoltJobSystem::flush() {
        std::lock_guard lock(m_tasks_mutex);
        for (auto* task : m_pending_tasks) {
            TaskSystem::raw().WaitforTask(task);
            delete task;
        }
        m_pending_tasks.clear();
    }

    JPH::JobHandle JoltJobSystem::CreateJob(const char* name, JPH::ColorArg color,
        const JobFunction& fn, JPH::uint32 deps) {

        uint32_t idx;
        for (;;) {
            idx = m_jobs.ConstructObject(name, color, this, fn, deps);
            if (idx != JPH::FixedSizeFreeList<Job>::cInvalidObjectIndex)
                break;

            // Pool exhausted, busy-wait - This should never happen
            JPH_ASSERT(false, "[JoltJobSystem] Job pool exhausted.");
        }

        Job* job = &m_jobs.Get(idx);
        JobHandle handle = JobHandle(job);

        // Queue immediately if no deps
        if (deps == 0)
            QueueJob(job);

        return handle;
    }

    void JoltJobSystem::FreeJob(JPH::JobSystem::Job* job) {
        m_jobs.DestructObject(job);
    }

    void JoltJobSystem::QueueJob(JPH::JobSystem::Job* job) {
        // Keep job alive while its in the enkiTS queue
        job->AddRef();

        struct JoltTask final : enki::ITaskSet {
            Job* m_job;
            explicit JoltTask(Job* j) : enki::ITaskSet(1), m_job(j) {}
            void ExecuteRange(enki::TaskSetPartition, uint32_t) override {
                m_job->Execute(); // no-op if barrier already ran it
                m_job->Release();
            }
        };

        auto* task = new JoltTask(job);
        {
            std::lock_guard lock(m_tasks_mutex);
            m_pending_tasks.push_back(task);
        }
        TaskSystem::raw().AddTaskSetToPipe(task);
    }

    void JoltJobSystem::QueueJobs(JPH::JobSystem::Job** jobs, uint32_t count) {
        for (uint32_t i = 0; i < count; ++i)
            QueueJob(jobs[i]);
    }

}
