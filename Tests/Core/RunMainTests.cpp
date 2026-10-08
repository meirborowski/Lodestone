#include "Lodestone/Core/RunMain.h"

#include "Common/LogCapture.h"
#include "Common/LogShutdownScope.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace Lodestone {

	namespace {

		const LogConfig QuietLog = {.Console = LogConsole::None};

		bool ContainsMessage(const std::vector<std::string>& messages, const std::string& message)
		{
			return std::ranges::find(messages, message) != messages.end();
		}

	}

	TEST_CASE("RunMain runs the body with the log initialized, then shuts the log down")
	{
		const Testing::LogShutdownScope shutdown;
		bool logInitializedInBody = false;

		const int exitCode = RunMain("Test app", QuietLog,
			[&logInitializedInBody]
			{
				logInitializedInBody = Log::IsInitialized();
				return 7;
			});

		CHECK(exitCode == 7);
		CHECK(logInitializedInBody);
		CHECK_FALSE(Log::IsInitialized());
	}

	TEST_CASE("RunMain reports an exception that escapes the body and fails")
	{
		const Testing::LogShutdownScope shutdown;
		const Testing::LogCapture capture;

		const int exitCode =
			RunMain("Test app", QuietLog, []() -> int { throw std::runtime_error("The disk is on fire"); });

		CHECK(exitCode == EXIT_FAILURE);
		CHECK(ContainsMessage(capture.GetMessages(),
			"Engine critical Test app stopped because of an unhandled exception: The disk is on fire"));
		CHECK_FALSE(Log::IsInitialized());
	}

	TEST_CASE("RunMain reports an exception that isn't derived from std::exception")
	{
		const Testing::LogShutdownScope shutdown;
		const Testing::LogCapture capture;

		const int exitCode = RunMain("Test app", QuietLog, []() -> int { throw 42; });

		CHECK(exitCode == EXIT_FAILURE);
		CHECK(ContainsMessage(capture.GetMessages(),
			"Engine critical Test app stopped because of an unhandled exception: an exception not derived from "
			"std::exception"));
	}

	TEST_CASE("RunMain fails without running the body when the log can't be initialized")
	{
		// The test runner has already initialized the log, so initializing it again fails
		REQUIRE(Log::IsInitialized());
		bool bodyRan = false;

		const int exitCode = RunMain("Test app", QuietLog,
			[&bodyRan]
			{
				bodyRan = true;
				return EXIT_SUCCESS;
			});

		CHECK(exitCode == EXIT_FAILURE);
		CHECK_FALSE(bodyRan);
		CHECK(Log::IsInitialized());
	}

}
