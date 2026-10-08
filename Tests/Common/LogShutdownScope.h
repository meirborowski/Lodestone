#pragma once

namespace Lodestone::Testing {

	// Shuts the log down for its lifetime, then re-initializes it the way the test runner does
	class LogShutdownScope
	{
	public:
		LogShutdownScope();
		~LogShutdownScope();

		LogShutdownScope(const LogShutdownScope&) = delete;
		LogShutdownScope& operator=(const LogShutdownScope&) = delete;
		LogShutdownScope(LogShutdownScope&&) = delete;
		LogShutdownScope& operator=(LogShutdownScope&&) = delete;
	};

}
