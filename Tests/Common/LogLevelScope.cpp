#include "Common/LogLevelScope.h"

namespace Lodestone::Testing {

	LogLevelScope::LogLevelScope(LogLevel level)
		: m_PreviousLevel(Log::GetLevel())
	{
		Log::SetLevel(level);
	}

	LogLevelScope::~LogLevelScope()
	{
		Log::SetLevel(m_PreviousLevel);
	}

}
