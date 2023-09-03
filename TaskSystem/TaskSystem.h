#pragma once

#include "Task.h"
#include "Executor.h"
#include "ThreadPool.h"

#include <iostream>
#include <vector>
#include <map>
#include <functional>
#include <mutex>
#include <set>

#define CW_BIND_EVENT_FN(fn)                                                                                           \
    [this](auto&&... args) -> decltype(auto) { return this->fn(std::forward<decltype(args)>(args)...); }

using Lock = std::unique_lock<std::mutex>;

namespace TaskSystem {

    /**
     * @brief The task system main class that can accept tasks to be scheduled and execute them on multiple threads
     *
     */
    struct TaskSystemExecutor {
    public:
        struct TaskID {
            TaskID() = default;

            std::unique_ptr<Executor> executor; // The executor that should run this task.
            int priority; // The priority of the task, used in the scheduler thread to check if it should switch.
            std::atomic_int state{0}; // 0 - inactive, 1 - progress, 2 - complete
            int taskId = 0; // A unique id for each task, only used to order tasks which have equal priority.
        };

        class Thread {
        public:
            Thread() = default;
            Thread(TaskSystemExecutor* owner, const std::string& name, int threadId);
            Thread(Thread&& other) = default;
            ~Thread() = default;

            void Run();

            /// Set the task this thread should be running.
            void SetTask(TaskSystemExecutor::TaskID* taskId) {
                m_TaskId.store(taskId, std::memory_order_release);
                Lock lock(m_Mutex);
                m_Run = true;
            }
        public:
            TaskSystemExecutor* m_Owner; // The owner of this thread object.
            std::thread m_Thread; // The native C++ thread.
            std::mutex m_Mutex; // A mutex used for the condition variable and to synchronize access to m_Run.
            std::string m_Name; // The name of the thread, used for debugger
            std::atomic<TaskSystemExecutor::TaskID*> m_TaskId; // The current task that is begin executed by this thread.
            int m_ThreadId; // The id of the thread that should be sent to the executor. A number in the range [0, threadCount).
            bool m_Run; // True if the thread is currently executing a task.
        };
    private:
        TaskSystemExecutor(int threadCount) : m_TaskQueue(Compare), m_ThreadCount(threadCount) {
            m_SchedulerThread = std::thread(CW_BIND_EVENT_FN(Run));
            for (int i = 0; i < m_ThreadCount; i++) {
                m_Threads.push_back(std::make_unique<Thread>(this, std::to_string(i), i));
            }
        }
    public:
        TaskSystemExecutor(const TaskSystemExecutor&) = delete;
        TaskSystemExecutor& operator=(const TaskSystemExecutor&) = delete;

        static TaskSystemExecutor& GetInstance();

        /**
         * @brief Initialization called once by the main application to allocate needed resources and start threads
         *
         * @param threadCount the desired number of threads to utilize
         */
        static void Init(int threadCount) {
            delete self;
            self = new TaskSystemExecutor(threadCount);
        }

        /**
         * @brief Schedule a task with specific priority to be executed
         *
         * @param task the parameters describing the task, Executor will be instantiated based on the expected name
         * @param priority the task priority, bigger means executer sooner
         * @return TaskID unique identifier used in later calls to wait or schedule callbacks for tasks
         */
        TaskID* ScheduleTask(std::unique_ptr<Task> task, int priority) {
            Lock lock(m_TaskMutex);
            
            // Create a TaskID object and just set checkTasks and wake up the
            // scheduler thread which will take care of the rest.
            TaskID* taskId = new TaskID();
            taskId->executor = std::unique_ptr<Executor>(executors[task->GetExecutorName()](std::move(task)));;
            taskId->priority = priority;

            taskId->taskId = m_TaskId++;
            taskId->state.store(0);

            m_CheckTasks = true;
            m_TaskQueue.insert(taskId);

            // Wake up the scheduler
            m_TaskReady.notify_one();

            return taskId;
        }

        void Join() {
            m_SchedulerThread.join();
            for (auto& thread : m_Threads) {
                thread->m_Thread.join();
            }
        }

        void Run() {
            while (true) {
                Lock lock(m_TaskMutex);
                while (!m_CheckTasks && !m_Done) {
                    m_TaskReady.wait(lock);
                }

                m_CheckTasks = false;
                if (m_Done) {
                    break;
                }
                for (auto iter = m_TaskQueue.begin(); iter != m_TaskQueue.end();) {
                    TaskID* highestPriorityTask = *iter;
                    if (highestPriorityTask->state == 2) {
                        // Remove the task if it finished.
                        iter = m_TaskQueue.erase(iter);
                        continue;
                    }
                    // Check if the task should change. Need to switch the task for all threads and wake them up.
                    if (highestPriorityTask != m_CurrentTask) {
                        m_ThreadsLeft = m_ThreadCount;
                        std::cout << "Switching tasks" << std::endl;
                        for (std::unique_ptr<Thread>& thread : m_Threads) {
                            thread->SetTask(highestPriorityTask);
                        }
                        m_NewTask.notify_all();
                        m_CurrentTask = highestPriorityTask;
                    }
                    // We only care about the task with highest priority.
                    break;
                }
            }
        }

        /**
         * @brief Blocking wait for a given task. Does not block if the task has already finished
         *
         * @param task the task to wait for
         */
        void WaitForTask(TaskID* task) {
            Lock lock(m_TaskMutex);

            while (task->state != 2) {
                m_TaskCompleted.wait(lock);
            }
        }

        static bool Compare(const TaskID* lhs, const TaskID* rhs) {
            if (lhs->priority == rhs->priority) {
                return lhs->taskId < rhs->taskId;
            }
            return lhs->priority > rhs->priority;
        }

        /**
         * @brief Register a callback to be executed when a task has finished executing. Executes the callback
         *        immediately if the task has already finished. The callback will be executed from any of the threads running the task.
         *
         * @param task the task that was previously scheduled
         * @param callback the callback to be executed
         */
        void OnTaskCompleted(TaskID *task, std::function<void(TaskID*)>&& callback) {
            // callback(task);
        }

        /**
         * @brief Load a dynamic library from a path and attempt to call OnLibraryInit
         *
         * @param path the path to the dynamic library
         * @return true when OnLibraryInit is found, false otherwise
         */
        bool LoadLibrary(const std::string& path);

        /**
         * @brief Register an executor with a name and constructor function. Should be called from
         *        inside the dynamic libraries defining executors.
         *
         * @param executorName the name associated with the executor
         * @param constructor constructor returning new instance of the executor
         */
        void Register(const std::string& executorName, ExecutorConstructor constructor) {
            Lock lock(m_TaskMutex);

            executors[executorName] = constructor;
        }

    private:
        std::thread m_SchedulerThread; // The native thread used for for thread scheduling.
        TaskID* m_CurrentTask; // The current task begin executed by the worker threads.
        bool m_Done = false; // Set to true when the TaskSystemExecutor should be destroyed.
        bool m_CheckTasks = true; // Tells the thread scheduler to check if the current active task should change. Set to true whenever a new task is queued.
        std::mutex m_TaskMutex; // Mutex used to synchronize access to the m_CheckTasks and the task queue.
        std::condition_variable m_TaskCompleted; // The condition variable used to wait in WaitForTask.
        std::condition_variable m_TaskReady; // The condition variable used for the scheduler thread.
        std::condition_variable m_NewTask; // Condition variable shared between the worker threads and the scheduler. Signaled when the task changes.
        std::vector<std::unique_ptr<Thread>> m_Threads; // The thread pool for this ThreadSystemExecutor.
        std::set<TaskID*, std::function<bool(const TaskID*, const TaskID*)>> m_TaskQueue; // The queue of tasks that should be executed.
        int m_TaskId = 0; // Counter used to assign ids to incoming tasks.
        std::atomic_int m_ThreadsLeft; // A counter shared between worker threads used to check if all threads are done executing the task.
        int m_ThreadCount; // The number of threads for the executor.

        static TaskSystemExecutor* self;
        std::map<std::string, ExecutorConstructor> executors;
    };

};