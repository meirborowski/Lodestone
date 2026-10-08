#include "Lodestone/Core/Assert.h"

#include "Lodestone/Core/Debugger.h"
#include "Lodestone/Core/Log.h"

#include <atomic>
#include <cstdlib>
#include <string>

namespace Lodestone {

	namespace {

		std::atomic<AssertHandler> s_AssertHandler = nullptr;

		[[noreturn]] void DefaultAssertHandler(const AssertionFailure& failure)
		{
			const std::string report =
				fmt::format("Assertion '{}' failed at {}:{}{}{}", failure.Condition, failure.Location.file_name(),
					failure.Location.line(), failure.Message.empty() ? "" : ": ", failure.Message);
			Log::ReportFatalError(
				failure.Origin == AssertOrigin::Core ? Log::GetCoreLogger() : Log::GetAppLogger(), report);

			if (IsDebuggerAttached())
				LS_DEBUGBREAK();

#if defined(_MSC_VER)
			// The failure is already reported, so stop the C runtime from showing an "abort() has been called" dialog
			// or a Windows Error Reporting prompt - either would hang CI and AI agents driving the engine
			_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
			std::abort();
		}

	}

	AssertHandler SetAssertHandler(AssertHandler handler)
	{
		return s_AssertHandler.exchange(handler);
	}

	namespace Detail {

		void HandleAssertionFailure(AssertOrigin origin, std::string_view condition, std::string_view message,
			const std::source_location& location)
		{
			const AssertionFailure failure{
				.Origin = origin, .Condition = condition, .Message = message, .Location = location};
			if (const AssertHandler handler = s_AssertHandler.load())
				handler(failure);
			else
				DefaultAssertHandler(failure);
		}

	}

}
