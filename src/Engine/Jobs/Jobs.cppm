module;

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

export module Engine.Jobs;

export class JobSystem {
  public:
    explicit JobSystem(size_t workers = defaultWorkerCount());
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    [[nodiscard]] size_t workerCount() const { return m_threads.size(); }
    void parallelFor(size_t count, size_t grain, const std::function<void(size_t, size_t)>& fn);

    [[nodiscard]] static size_t defaultWorkerCount();

  private:
    struct Job {
        const std::function<void(size_t, size_t)>* fn = nullptr;
        size_t count = 0;
        size_t grain = 1;
        std::atomic<size_t> next{0};
    };

    static void drain(Job& job);
    void workerLoop();

    std::vector<std::thread> m_threads;
    std::mutex m_submit;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::condition_variable m_done;
    Job* m_job = nullptr;
    uint64_t m_generation = 0;
    size_t m_active = 0;
    bool m_stop = false;
};
