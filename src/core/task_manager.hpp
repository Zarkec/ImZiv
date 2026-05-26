#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace hex {

    class Task {
    public:
        Task(std::string name, uint64_t maxValue, bool background, bool blocking, std::function<void(Task&)> function);
        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;
        ~Task();

        void update(uint64_t value);
        void update() const;
        void increment();
        void setMaxValue(uint64_t value);
        void interrupt();

        [[nodiscard]] bool isBackgroundTask() const;
        [[nodiscard]] bool isBlocking() const;
        [[nodiscard]] bool isFinished() const;
        [[nodiscard]] bool hadException() const;
        [[nodiscard]] bool wasInterrupted() const;
        [[nodiscard]] bool shouldInterrupt() const;
        [[nodiscard]] std::string getExceptionMessage() const;
        [[nodiscard]] const std::string& getName() const;
        [[nodiscard]] uint64_t getValue() const;
        [[nodiscard]] uint64_t getMaxValue() const;

        void wait() const;

    private:
        struct Interrupt final : public std::exception {
            [[nodiscard]] const char* what() const noexcept override { return "Task interrupted"; }
        };

        void finish();
        void markInterrupted();
        void markException(const char* message);

        mutable std::mutex m_mutex;
        mutable std::condition_variable m_finishedCondition;
        std::string m_name;
        std::function<void(Task&)> m_function;
        std::atomic<uint64_t> m_currValue { 0 };
        std::atomic<uint64_t> m_maxValue { 0 };
        std::atomic_bool m_shouldInterrupt { false };
        std::atomic_bool m_background { true };
        std::atomic_bool m_blocking { false };
        std::atomic_bool m_finished { false };
        std::atomic_bool m_interrupted { false };
        std::atomic_bool m_hadException { false };
        std::string m_exceptionMessage;

        friend class TaskManager;
    };

    class TaskHolder {
    public:
        TaskHolder() = default;
        explicit TaskHolder(std::weak_ptr<Task> task);

        [[nodiscard]] bool isRunning() const;
        [[nodiscard]] bool hadException() const;
        [[nodiscard]] bool wasInterrupted() const;
        [[nodiscard]] bool shouldInterrupt() const;
        [[nodiscard]] uint32_t getProgress() const;
        [[nodiscard]] std::string getExceptionMessage() const;

        void interrupt() const;
        void wait() const;

    private:
        std::weak_ptr<Task> m_task;
    };

    class TaskManager {
    public:
        TaskManager() = delete;

        static constexpr uint64_t NoProgress = 0;

        static void init();
        static void exit();

        static TaskHolder createTask(const std::string& name, uint64_t maxValue, std::function<void(Task&)> function);
        static TaskHolder createTask(const std::string& name, uint64_t maxValue, std::function<void()> function);
        static TaskHolder createBackgroundTask(const std::string& name, std::function<void(Task&)> function);
        static TaskHolder createBackgroundTask(const std::string& name, std::function<void()> function);
        static TaskHolder createBlockingTask(const std::string& name, uint64_t maxValue, std::function<void(Task&)> function);
        static TaskHolder createBlockingTask(const std::string& name, uint64_t maxValue, std::function<void()> function);

        static void doLater(const std::function<void()>& function);
        static void runDeferredCalls();
        static void collectGarbage();

        static size_t getRunningTaskCount();
        static size_t getRunningBackgroundTaskCount();
        static size_t getRunningBlockingTaskCount();

    private:
        static TaskHolder createTask(const std::string& name, uint64_t maxValue, bool background, bool blocking, std::function<void(Task&)> function);
    };

}
