#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>

namespace Lodestone {

	// Runs work from other threads on the main thread. The editor's state isn't thread-safe, so MCP transports, which
	// receive requests on threads of their own, hand each request to the main loop and wait for the answer
	class MainThreadDispatcher
	{
	public:
		MainThreadDispatcher() = default;
		~MainThreadDispatcher();

		MainThreadDispatcher(const MainThreadDispatcher&) = delete;
		MainThreadDispatcher& operator=(const MainThreadDispatcher&) = delete;
		MainThreadDispatcher(MainThreadDispatcher&&) = delete;
		MainThreadDispatcher& operator=(MainThreadDispatcher&&) = delete;

		// Runs a function on the main thread and waits for its result. Returns nothing if the dispatcher shuts down
		// before running it; rethrows what the function throws. Call from any thread but the main one, which would
		// wait forever
		template <typename Function>
		std::optional<std::invoke_result_t<Function>> Invoke(Function function)
		{
			using Result = std::invoke_result_t<Function>;
			auto promise = std::make_shared<std::promise<Result>>();
			std::future<Result> future = promise->get_future();
			// The task holds the only reference to the promise, so dropping the task breaks the promise. What the
			// function throws is rethrown to the caller, as std::async does, and never reaches the main loop.
			// The analyzer loses track of the promise as it moves into std::function, and reports a leak that can't
			// happen: the task owns it
			// NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
			if (!Post(
					[result = std::move(promise), work = std::move(function)]() mutable
					{
						try
						{
							result->set_value(work());
						}
						catch (...)
						{
							result->set_exception(std::current_exception());
						}
					}))
				return std::nullopt;
			// A task dropped at shutdown never sets its promise, which breaks it
			try
			{
				return future.get();
			}
			catch (const std::future_error&)
			{
				return std::nullopt;
			}
		}

		// Runs every task queued so far. Call from the main thread; returns how many ran
		size_t RunPending();
		// Waits until a task is queued, the timeout passes or the dispatcher shuts down
		void WaitForWork(std::chrono::milliseconds timeout);
		// Drops queued tasks and refuses new ones. Their callers get nothing
		void Shutdown() noexcept;

	private:
		bool Post(std::function<void()> task);

	private:
		std::mutex m_Mutex;
		std::condition_variable m_WorkAvailable;
		std::deque<std::function<void()>> m_Tasks;
		bool m_ShutDown = false;
	};

}
