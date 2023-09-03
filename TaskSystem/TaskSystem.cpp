#include "TaskSystem.h"

#include <cassert>
#include <iostream>

#if defined(_WIN32) || defined(_WIN64)
#define USE_WIN
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#undef LoadLibrary
#else
#include <dlfcn.h>
#endif

namespace TaskSystem {

TaskSystemExecutor* TaskSystemExecutor::self = nullptr;

TaskSystemExecutor &TaskSystemExecutor::GetInstance() {
    return *self;
}

TaskSystemExecutor::Thread::Thread(TaskSystemExecutor *owner, const std::string& name, int threadId) : m_Name(name), m_TaskId(nullptr), m_ThreadId(threadId), m_Owner(owner) {
    m_Thread = std::thread(CW_BIND_EVENT_FN(Run));
}

void TaskSystemExecutor::Thread::Start(std::function<void()> worker) {
}

void TaskSystemExecutor::Thread::Run() {

	// m_ThreadStarted = true;
	// m_Started.notify_one();

	while (true) {
        {
            Lock lock(m_Mutex);
            while (!m_Run) {
                m_Owner->m_NewTask.wait(lock);
            }
        }

        TaskSystemExecutor::TaskID* task=nullptr;
		do {
			task = m_TaskId.load(std::memory_order_acquire);
		} while (task && task->executor->ExecuteStep(m_ThreadId, m_Owner->m_ThreadCount) != Executor::ExecStatus::ES_Stop);
		{
			Lock lock(m_Mutex);
			m_Run = false;
		}

        m_Owner->m_ThreadsLeft--;
        if (m_Owner->m_ThreadsLeft <= 0 && task) {
            std::cout << m_Owner->m_ThreadsLeft << std::endl;
            task->state = 2;
            m_Owner->m_TaskCompleted.notify_one();
            m_Owner->m_TaskReady.notify_one();
            m_Owner->m_CheckTasks = true;
        }
	}
}

bool TaskSystemExecutor::LoadLibrary(const std::string &path) {
#ifdef USE_WIN
    HMODULE handle = LoadLibraryA(path.c_str());
#else
    void *handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    assert(handle);
    if (handle) {
        OnLibraryInitPtr initLib =
#ifdef USE_WIN
            (OnLibraryInitPtr)GetProcAddress(handle, "OnLibraryInit");
#else
            (OnLibraryInitPtr)dlsym(handle, "OnLibraryInit");
#endif
        assert(initLib);
        if (initLib) {
            initLib(*this);
            printf("Initialized [%s] executor\n", path.c_str());
            return true;
        }
    }
    return false;
}

};
