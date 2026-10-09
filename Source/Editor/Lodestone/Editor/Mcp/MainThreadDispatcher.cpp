#include "Lodestone/Editor/Mcp/MainThreadDispatcher.h"

#include <utility>

namespace Lodestone {

	MainThreadDispatcher::~MainThreadDispatcher()
	{
		Shutdown();
	}

	size_t MainThreadDispatcher::RunPending()
	{
		std::deque<std::function<void()>> tasks;
		{
			const std::scoped_lock lock(m_Mutex);
			tasks.swap(m_Tasks);
		}
		for (const std::function<void()>& task : tasks)
			task();
		return tasks.size();
	}

	void MainThreadDispatcher::WaitForWork(std::chrono::milliseconds timeout)
	{
		std::unique_lock lock(m_Mutex);
		m_WorkAvailable.wait_for(lock, timeout, [this] { return !m_Tasks.empty() || m_ShutDown; });
	}

	void MainThreadDispatcher::Shutdown() noexcept
	{
		{
			const std::scoped_lock lock(m_Mutex);
			m_ShutDown = true;
			// Destroying the tasks breaks their promises, which wakes their callers. They return without coming back
			// to the dispatcher, so this can happen under the lock
			m_Tasks.clear();
		}
		m_WorkAvailable.notify_all();
	}

	bool MainThreadDispatcher::Post(std::function<void()> task)
	{
		{
			const std::scoped_lock lock(m_Mutex);
			if (m_ShutDown)
				return false;
			m_Tasks.push_back(std::move(task));
		}
		m_WorkAvailable.notify_all();
		return true;
	}

}
