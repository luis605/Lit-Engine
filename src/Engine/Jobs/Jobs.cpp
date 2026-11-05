module;

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

module Engine.Jobs;

size_t JobSystem::defaultWorkerCount() {
    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    return hardwareThreads > 1 ? hardwareThreads - 1 : 0;
}

JobSystem::JobSystem(size_t workerTotal) {
    m_threads.reserve(workerTotal);
    for (size_t worker = 0; worker < workerTotal; ++worker) m_threads.emplace_back([this] { workerLoop(); });
}

JobSystem::~JobSystem() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    for (std::thread& thread : m_threads) thread.join();
}

void JobSystem::drain(Job& job) {
    while (true) {
        const size_t begin = job.next.fetch_add(job.grain);
        if (begin >= job.count) return;
        (*job.body)(begin, std::min(begin + job.grain, job.count));
    }
}

void JobSystem::workerLoop() {
    uint64_t seenGeneration = 0;
    while (true) {
        Job* job = nullptr;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [&] { return m_stop || m_generation != seenGeneration; });
            if (m_stop) return;
            seenGeneration = m_generation;
            job = m_job;
        }
        drain(*job);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (--m_active == 0) m_done.notify_all();
        }
    }
}

void JobSystem::parallelFor(size_t count, size_t grain, const std::function<void(size_t, size_t)>& body) {
    if (count == 0) return;
    grain = std::max<size_t>(grain, 1);
    if (m_threads.empty() || count <= grain) {
        body(0, count);
        return;
    }

    std::lock_guard<std::mutex> submitLock(m_submitMutex);
    Job job;
    job.body = &body;
    job.count = count;
    job.grain = grain;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_job = &job;
        m_active = m_threads.size();
        ++m_generation;
    }
    m_wake.notify_all();
    drain(job);
    std::unique_lock<std::mutex> lock(m_mutex);
    m_done.wait(lock, [&] { return m_active == 0; });
    m_job = nullptr;
}
