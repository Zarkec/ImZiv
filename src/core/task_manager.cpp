#include "core/task_manager.hpp"

#include <algorithm>

namespace hex {
namespace {
    std::mutex g_queueMutex;
    std::recursive_mutex g_deferredMutex;
    std::condition_variable g_jobCondition;
    std::list<std::shared_ptr<Task>> g_tasks;
    std::list<std::shared_ptr<Task>> g_taskQueue;
    std::list<std::function<void()>> g_deferredCalls;
    std::vector<std::thread> g_workers;
    std::atomic_bool g_stopping { false };
}

    Task::Task(std::string name, uint64_t maxValue, bool background, bool blocking, std::function<void(Task&)> function)
        : m_name(std::move(name)),
          m_function(std::move(function)),
          m_maxValue(maxValue),
          m_background(background),
          m_blocking(blocking) {}

    Task::~Task() {
        if (!isFinished())
            interrupt();
    }

    void Task::update(uint64_t value) {
        m_currValue.store(value, std::memory_order_relaxed);
        update();
    }

    void Task::update() const {
        if (m_shouldInterrupt.load(std::memory_order_relaxed))
            throw Interrupt();
    }

    void Task::increment() {
        m_currValue.fetch_add(1, std::memory_order_relaxed);
        update();
    }

    void Task::setMaxValue(uint64_t value) {
        m_maxValue.store(value, std::memory_order_relaxed);
    }

    void Task::interrupt() {
        m_shouldInterrupt.store(true, std::memory_order_relaxed);
    }

    bool Task::isBackgroundTask() const {
        return m_background.load(std::memory_order_relaxed);
    }

    bool Task::isBlocking() const {
        return m_blocking.load(std::memory_order_relaxed);
    }

    bool Task::isFinished() const {
        return m_finished.load(std::memory_order_acquire);
    }

    bool Task::hadException() const {
        return m_hadException.load(std::memory_order_acquire);
    }

    bool Task::wasInterrupted() const {
        return m_interrupted.load(std::memory_order_acquire);
    }

    bool Task::shouldInterrupt() const {
        return m_shouldInterrupt.load(std::memory_order_relaxed);
    }

    std::string Task::getExceptionMessage() const {
        std::scoped_lock lock(m_mutex);
        return m_exceptionMessage;
    }

    const std::string& Task::getName() const {
        return m_name;
    }

    uint64_t Task::getValue() const {
        return m_currValue.load(std::memory_order_relaxed);
    }

    uint64_t Task::getMaxValue() const {
        return m_maxValue.load(std::memory_order_relaxed);
    }

    void Task::wait() const {
        std::unique_lock lock(m_mutex);
        m_finishedCondition.wait(lock, [this] { return isFinished(); });
    }

    void Task::finish() {
        {
            std::scoped_lock lock(m_mutex);
            m_finished.store(true, std::memory_order_release);
        }
        m_finishedCondition.notify_all();
    }

    void Task::markInterrupted() {
        m_interrupted.store(true, std::memory_order_release);
    }

    void Task::markException(const char* message) {
        std::scoped_lock lock(m_mutex);
        m_exceptionMessage = message != nullptr ? message : "Unknown exception";
        m_hadException.store(true, std::memory_order_release);
    }

    TaskHolder::TaskHolder(std::weak_ptr<Task> task) : m_task(std::move(task)) {}

    bool TaskHolder::isRunning() const {
        auto task = m_task.lock();
        return task != nullptr && !task->isFinished();
    }

    bool TaskHolder::hadException() const {
        auto task = m_task.lock();
        return task != nullptr && task->hadException();
    }

    bool TaskHolder::wasInterrupted() const {
        auto task = m_task.lock();
        return task != nullptr && task->wasInterrupted();
    }

    bool TaskHolder::shouldInterrupt() const {
        auto task = m_task.lock();
        return task != nullptr && task->shouldInterrupt();
    }

    uint32_t TaskHolder::getProgress() const {
        auto task = m_task.lock();
        if (task == nullptr || task->getMaxValue() == 0)
            return 0;
        return uint32_t((task->getValue() * 100) / task->getMaxValue());
    }

    std::string TaskHolder::getExceptionMessage() const {
        auto task = m_task.lock();
        return task != nullptr ? task->getExceptionMessage() : std::string();
    }

    void TaskHolder::interrupt() const {
        auto task = m_task.lock();
        if (task != nullptr)
            task->interrupt();
    }

    void TaskHolder::wait() const {
        auto task = m_task.lock();
        if (task != nullptr)
            task->wait();
    }

    void TaskManager::init() {
        if (!g_workers.empty())
            return;

        g_stopping.store(false, std::memory_order_release);
        unsigned int threadCount = std::thread::hardware_concurrency();
        if (threadCount == 0)
            threadCount = 2;

        for (unsigned int i = 0; i < threadCount; ++i) {
            g_workers.emplace_back([] {
                while (true) {
                    std::shared_ptr<Task> task;
                    {
                        std::unique_lock lock(g_queueMutex);
                        g_jobCondition.wait(lock, [] {
                            return g_stopping.load(std::memory_order_acquire) || !g_taskQueue.empty();
                        });

                        if (g_stopping.load(std::memory_order_acquire) && g_taskQueue.empty())
                            break;

                        task = std::move(g_taskQueue.front());
                        g_taskQueue.pop_front();
                    }

                    try {
                        task->m_function(*task);
                    } catch (const Task::Interrupt&) {
                        task->markInterrupted();
                    } catch (const std::exception& e) {
                        task->markException(e.what());
                    } catch (...) {
                        task->markException("Unknown exception");
                    }

                    task->finish();
                }
            });
        }
    }

    void TaskManager::exit() {
        {
            std::scoped_lock lock(g_queueMutex);
            for (const auto& task : g_tasks)
                task->interrupt();
            g_stopping.store(true, std::memory_order_release);
        }

        g_jobCondition.notify_all();

        for (auto& worker : g_workers) {
            if (worker.joinable())
                worker.join();
        }

        g_workers.clear();
        g_tasks.clear();
        g_taskQueue.clear();
        g_deferredCalls.clear();
    }

    TaskHolder TaskManager::createTask(const std::string& name, uint64_t maxValue, bool background, bool blocking, std::function<void(Task&)> function) {
        std::scoped_lock lock(g_queueMutex);
        auto task = std::make_shared<Task>(name, maxValue, background, blocking, std::move(function));
        g_tasks.emplace_back(task);
        g_taskQueue.emplace_back(task);
        g_jobCondition.notify_one();
        return TaskHolder(task);
    }

    TaskHolder TaskManager::createTask(const std::string& name, uint64_t maxValue, std::function<void(Task&)> function) {
        return createTask(name, maxValue, false, false, std::move(function));
    }

    TaskHolder TaskManager::createTask(const std::string& name, uint64_t maxValue, std::function<void()> function) {
        return createTask(name, maxValue, [function = std::move(function)](Task&) { function(); });
    }

    TaskHolder TaskManager::createBackgroundTask(const std::string& name, std::function<void(Task&)> function) {
        return createTask(name, NoProgress, true, false, std::move(function));
    }

    TaskHolder TaskManager::createBackgroundTask(const std::string& name, std::function<void()> function) {
        return createBackgroundTask(name, [function = std::move(function)](Task&) { function(); });
    }

    TaskHolder TaskManager::createBlockingTask(const std::string& name, uint64_t maxValue, std::function<void(Task&)> function) {
        return createTask(name, maxValue, true, true, std::move(function));
    }

    TaskHolder TaskManager::createBlockingTask(const std::string& name, uint64_t maxValue, std::function<void()> function) {
        return createBlockingTask(name, maxValue, [function = std::move(function)](Task&) { function(); });
    }

    void TaskManager::doLater(const std::function<void()>& function) {
        std::scoped_lock lock(g_deferredMutex);
        g_deferredCalls.push_back(function);
    }

    void TaskManager::runDeferredCalls() {
        std::scoped_lock lock(g_deferredMutex);
        while (!g_deferredCalls.empty()) {
            auto callback = std::move(g_deferredCalls.front());
            g_deferredCalls.pop_front();
            callback();
        }
    }

    void TaskManager::collectGarbage() {
        std::scoped_lock lock(g_queueMutex);
        g_tasks.remove_if([](const std::shared_ptr<Task>& task) {
            return task->isFinished() && !task->hadException();
        });
    }

    size_t TaskManager::getRunningTaskCount() {
        std::scoped_lock lock(g_queueMutex);
        return std::count_if(g_tasks.begin(), g_tasks.end(), [](const std::shared_ptr<Task>& task) {
            return !task->isBackgroundTask();
        });
    }

    size_t TaskManager::getRunningBackgroundTaskCount() {
        std::scoped_lock lock(g_queueMutex);
        return std::count_if(g_tasks.begin(), g_tasks.end(), [](const std::shared_ptr<Task>& task) {
            return task->isBackgroundTask();
        });
    }

    size_t TaskManager::getRunningBlockingTaskCount() {
        std::scoped_lock lock(g_queueMutex);
        return std::count_if(g_tasks.begin(), g_tasks.end(), [](const std::shared_ptr<Task>& task) {
            return task->isBlocking();
        });
    }

}
