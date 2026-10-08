#pragma once

#include "Lodestone/Core/Log.h"

namespace Lodestone::Testing {

	// Sets the log level for its lifetime, then restores the previous level
	class LogLevelScope
	{
	public:
		explicit LogLevelScope(LogLevel level);
		~LogLevelScope();

		LogLevelScope(const LogLevelScope&) = delete;
		LogLevelScope& operator=(const LogLevelScope&) = delete;
		LogLevelScope(LogLevelScope&&) = delete;
		LogLevelScope& operator=(LogLevelScope&&) = delete;

	private:
		LogLevel m_PreviousLevel;
	};

}
