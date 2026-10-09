#pragma once

// Lodestone's EnTT configuration. EnTT includes <entt/ext/config.h> before its own configuration whenever the header
// exists, and LodestoneCore puts this directory on the include path of everything that uses EnTT through it.

#include "Lodestone/Core/Assert.h"

#include <source_location>

// EnTT's internal checks report through the engine's assert handler, like LS_CORE_ASSERT, instead of the C runtime's
// assert(), so a failure is logged and tests can intercept it. An expression, as EnTT uses it in expressions
#if LS_ENABLE_ASSERTS
	#define ENTT_ASSERT(condition, message)                         \
		((condition) ? static_cast<void>(0)                         \
					 : ::Lodestone::Detail::HandleAssertionFailure( \
						   ::Lodestone::AssertOrigin::Core, #condition, message, std::source_location::current()))
#else
	#define ENTT_DISABLE_ASSERT
#endif

// Empty components (tags) are stored like any other, so the reflection registry handles every component the same way
#define ENTT_NO_ETO
