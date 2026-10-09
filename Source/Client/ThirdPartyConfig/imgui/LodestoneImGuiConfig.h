#pragma once

// Lodestone's Dear ImGui configuration. The imgui target defines IMGUI_USER_CONFIG as this header, so imconfig.h
// includes it wherever imgui.h is included (see cmake/Dependencies.cmake).

#include "Lodestone/Core/Assert.h"

#include <source_location>

// Dear ImGui's checks report through the engine's assert handler, like LS_CORE_ASSERT, instead of the C runtime's
// assert(), so a failure is logged without a dialog and tests can intercept it. An expression, like assert()
#if LS_ENABLE_ASSERTS
	#define IM_ASSERT(condition)                                                                                \
		((condition) ? static_cast<void>(0)                                                                     \
					 : ::Lodestone::Detail::HandleAssertionFailure(::Lodestone::AssertOrigin::Core, #condition, \
						   "Dear ImGui's check failed", std::source_location::current()))
#else
	#define IM_ASSERT(condition) static_cast<void>(0)
#endif
