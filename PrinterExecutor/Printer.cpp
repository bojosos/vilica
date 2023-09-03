#include "Task.h"
#include "Executor.h"
#include "TaskSystem.h"

#include <chrono>
#include <thread>
#include <atomic>

struct Printer : TaskSystem::Executor {
    Printer(std::unique_ptr<TaskSystem::Task> taskToExecute) : Executor(std::move(taskToExecute)) {
        max = task->GetIntParam("max").value();
        sleepMs = task->GetIntParam("sleep").value();
        name = task->GetStringParam("name").value();
    }

    virtual ~Printer() {}

    virtual ExecStatus ExecuteStep(int threadIndex, int threadCount) {
        const int myValue = current.fetch_add(1);
        if (myValue >= max) {
            current.fetch_sub(1);
            return ExecStatus::ES_Stop;
        }

        printf("Printer %s [%d/%d]: %d\n", name.c_str(), threadIndex+1, threadCount, myValue);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        return ExecStatus::ES_Continue;
    };

    std::atomic<int> current = 0;
    int max = 0;
    int sleepMs = 0;
    std::string name;
};

TaskSystem::Executor* ExecutorConstructorImpl(std::unique_ptr<TaskSystem::Task> taskToExecute) {
    return new Printer(std::move(taskToExecute));
}

IMPLEMENT_ON_INIT() {
    ts.Register("printer", &ExecutorConstructorImpl);
}
