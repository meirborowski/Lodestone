#include "Common/LogShutdownScope.h"

#include "Lodestone/Core/Log.h"

#include <doctest/doctest.h>

namespace Lodestone::Testing {

	LogShutdownScope::LogShutdownScope()
	{
		Log::Shutdown();
	}

	LogShutdownScope::~LogShutdownScope()
	{
		Log::Shutdown();
		const auto result = Log::Init();
		CHECK_MESSAGE(result.has_value(), "Can't re-initialize the log after the test");
	}

}
