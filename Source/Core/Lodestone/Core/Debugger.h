#pragma once

#if !defined(_MSC_VER) && !defined(__clang__)
	#include <csignal>
#endif

namespace Lodestone {

	// Returns whether a debugger is attached to this process
	[[nodiscard]]   bool IsDebuggerAttached( );

}

// Breaks into the attached debugger. Without one, this terminates the process - check IsDebuggerAttached() first
#if defined(_MSC_VER)
	#define LS_DEBUGBREAK() __debugbreak()
#elif defined(__clang__)
	#define LS_DEBUGBREAK() __builtin_debugtrap()
#else
	#define LS_DEBUGBREAK() static_cast<void>(std::raise(SIGTRAP))
#endif
