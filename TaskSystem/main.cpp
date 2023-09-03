#include "TaskSystem.h"

#include <cassert>
#include <chrono>
#include <thread>

#include "Windows.h"
#undef min
#undef max
#undef LoadLibrary

using namespace TaskSystem;

struct PrinterParams : Task {
    int max;
    int sleep;
    std::string printerName;

    PrinterParams(const std::string &name, int max, int sleep): max(max), sleep(sleep), printerName(name) {}
    virtual std::optional<int> GetIntParam(const std::string &name) const override {
        if (name == "max") {
            return max;
        } else if (name == "sleep") {
            return sleep;
        }
        return std::nullopt;
    }
    virtual std::optional<std::string> GetStringParam(const std::string& name) const override {
		if (name == "name") {
			return printerName;
		}
    }
    virtual std::string GetExecutorName() const { return "printer"; }
};


struct RaytracerParams : Task {
    std::string sceneName;

    RaytracerParams(const std::string &sceneName): sceneName(sceneName) {}
    virtual std::optional<std::string> GetStringParam(const std::string &name) const {
        if (name == "sceneName") {
            return sceneName;
        }
        return std::nullopt;
    }
    virtual std::string GetExecutorName() const { return "raytracer"; }
};

void testRenderer() {
    TaskSystemExecutor &ts = TaskSystemExecutor::GetInstance();

#if defined(_WIN32)
    const bool libLoaded = ts.LoadLibrary("RaytracerExecutor.dll");
#else
    const bool libLoaded = ts.LoadLibrary("libRaytracerExecutor.dylib");
#endif
    assert(libLoaded);
    std::unique_ptr<Task> task = std::make_unique<RaytracerParams>("HeavyMesh");

	TaskSystemExecutor::TaskID *id = ts.ScheduleTask(std::move(task), 1);
    ts.WaitForTask(id);
}

void testPrinter() {
    TaskSystemExecutor &ts = TaskSystemExecutor::GetInstance();
#if defined(_WIN32) || defined(_WIN64)
    const bool libLoaded = ts.LoadLibrary("PrinterExecutor.dll");
#elif defined(__APPLE__)
    const bool libLoaded = ts.LoadLibrary("libPrinterExecutor.dylib");
#elif defined(__linux__)
    const bool libLoaded = ts.LoadLibrary("libPrinterExecutor.so");
#endif
    assert(libLoaded);

    // two instances of the same task
    std::unique_ptr<Task> p1 = std::make_unique<PrinterParams>("Printer lower", 100, 25);
    std::unique_ptr<Task> p2 = std::make_unique<PrinterParams>("Printer higher", 100, 25);

    // give some time for the first task to execute
    TaskSystemExecutor::TaskID *id1 = ts.ScheduleTask(std::move(p1), 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // insert bigger priority task, TaskSystem should switch to it
    TaskSystemExecutor::TaskID *id2 = ts.ScheduleTask(std::move(p2), 26);

    ts.OnTaskCompleted(id1, [](TaskSystemExecutor::TaskID *id) {
        printf("Task 1 finished\n");
    });
    ts.WaitForTask(id2);
    ts.WaitForTask(id1);
}

int main(int argc, char *argv[]) {
    TaskSystemExecutor::Init(8);

    testPrinter();

    return 0;
}