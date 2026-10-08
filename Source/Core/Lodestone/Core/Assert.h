#pragma once

#include "Lodestone/Core/Base.h"

#include <spdlog/fmt/fmt.h>

#include <source_location>
#include <string>
#include <string_view>
#include <utility>

namespace Lodestone {

	// Whether an assert is in engine code (LS_CORE_ASSERT) or application code (LS_ASSERT). It decides which logger
	// reports the failure
	enum class AssertOrigin
	{
		Core,
		App
	};

	// Describes a failed assert. The strings are only valid during the call to the assert handler
	struct AssertionFailure
	{
		AssertOrigin Origin = AssertOrigin::Core;
		std::string_view Condition;
		std::string_view Message;
		std::source_location Location;
	};

	// Called when an assert fails. The default handler logs the failure, breaks into an attached debugger and aborts.
	// If a replacement handler returns, execution continues after the failed assert - only tests should rely on this
	using AssertHandler = void (*)(const AssertionFailure& failure);

	// Installs a handler for failed asserts, returning the previous one. Passing nullptr restores the default handler.
	// Thread-safe
	AssertHandler SetAssertHandler(AssertHandler handler);

	namespace Detail {

		void HandleAssertionFailure(AssertOrigin origin, std::string_view condition, std::string_view message,
			const std::source_location& location);

		inline std::string FormatAssertMessage()
		{
			return {};
		}

		template <typename... Args>
		std::string FormatAssertMessage(fmt::format_string<Args...> format, Args&&... args)
		{
			return fmt::format(format, std::forward<Args>(args)...);
		}

	}

}

#if LS_ENABLE_ASSERTS
	#define LS_INTERNAL_ASSERT(origin, condition, ...)                                                       \
		do                                                                                                   \
		{                                                                                                    \
			if (!(condition)) [[unlikely]]                                                                   \
				::Lodestone::Detail::HandleAssertionFailure(origin, #condition,                              \
					::Lodestone::Detail::FormatAssertMessage(__VA_ARGS__), std::source_location::current()); \
		} while (false)
#else
	// The condition and message still compile, unevaluated, so they can't rot in Dist builds and the variables they
	// use don't trigger unused-variable warnings
	#define LS_INTERNAL_ASSERT(origin, condition, ...) \
		static_cast<void>(                             \
			sizeof(static_cast<bool>(condition)) + sizeof(::Lodestone::Detail::FormatAssertMessage(__VA_ARGS__)))
#endif

// Checks an invariant: LS_CORE_ASSERT(index < size, "Index {} is out of range", index). The message is optional, and is
// a fmt format string followed by its arguments.
// Asserts are active in Debug and Release and compiled out of Dist, so the condition must not have side effects.
// LS_CORE_ASSERT is for engine code, LS_ASSERT for application code
#define LS_CORE_ASSERT(condition, ...) \
	LS_INTERNAL_ASSERT(::Lodestone::AssertOrigin::Core, condition __VA_OPT__(, ) __VA_ARGS__)
#define LS_ASSERT(condition, ...) \
	LS_INTERNAL_ASSERT(::Lodestone::AssertOrigin::App, condition __VA_OPT__(, ) __VA_ARGS__)
