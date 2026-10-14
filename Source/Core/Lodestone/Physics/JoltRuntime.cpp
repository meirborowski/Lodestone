#include "Lodestone/Physics/JoltRuntime.h"

#include "Lodestone/Core/Assert.h"
#include "Lodestone/Core/Log.h"

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/IssueReporting.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/RegisterTypes.h>

#include <array>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <source_location>

namespace Lodestone {

	namespace {

		// Jolt reports problems it recovers from, such as a shape it can't build, as traces
		// NOLINTNEXTLINE(modernize-avoid-variadic-functions): Jolt's trace hook is a printf-style function
		void TraceToLog(const char* format, ...)
		{
			std::array<char, 1024> message{};
			va_list arguments;
			va_start(arguments, format);
			std::vsnprintf(message.data(), message.size(), format, arguments);
			va_end(arguments);
			LS_CORE_WARN("Jolt: {}", message.data());
		}

#if defined(JPH_ENABLE_ASSERTS)
		bool ReportAssertFailure(const char* expression, const char* message, const char* file, JPH::uint line)
		{
			Detail::HandleAssertionFailure(AssertOrigin::Core, expression,
				fmt::format("Jolt's check failed at {}:{}{}{}", file, line, message != nullptr ? ": " : "",
					message != nullptr ? message : ""),
				std::source_location::current());
			// The engine's handler has reported the failure, and broken into the debugger if one is attached
			return false;
		}
#endif

	}

	void InitializeJoltRuntime()
	{
		static std::once_flag s_Initialized;
		std::call_once(s_Initialized,
			[]
			{
				JPH::RegisterDefaultAllocator();
				JPH::Trace = TraceToLog;
#if defined(JPH_ENABLE_ASSERTS)
				JPH::AssertFailed = ReportAssertFailure;
#endif
				// Never deleted: Jolt stays set up until the process exits
				JPH::Factory::sInstance = new JPH::Factory();
				JPH::RegisterTypes();
			});
	}

}
