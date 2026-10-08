#pragma once

#include <memory>
#include <utility>

// Platform
#if defined(_WIN32)
	#define LS_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
	#define LS_PLATFORM_MACOS 1
#elif defined(__linux__)
	#define LS_PLATFORM_LINUX 1
#else
	#error "Unsupported platform: Lodestone runs on Windows, macOS and Linux"
#endif

// Build configuration: the build system defines exactly one of LS_CONFIG_DEBUG, LS_CONFIG_RELEASE and LS_CONFIG_DIST
#if (defined(LS_CONFIG_DEBUG) + defined(LS_CONFIG_RELEASE) + defined(LS_CONFIG_DIST)) != 1
	#error "Exactly one of LS_CONFIG_DEBUG, LS_CONFIG_RELEASE and LS_CONFIG_DIST must be defined"
#endif

// Asserts and developer logging (trace, debug and info messages) are compiled out of Dist builds
#if defined(LS_CONFIG_DIST)
	#define LS_ENABLE_ASSERTS 0
	#define LS_ENABLE_DEVELOPER_LOGGING 0
#else
	#define LS_ENABLE_ASSERTS 1
	#define LS_ENABLE_DEVELOPER_LOGGING 1
#endif

namespace Lodestone {

	// Unique ownership
	template <typename T>
	using Scope = std::unique_ptr<T>;

	template <typename T, typename... Args>
	[[nodiscard]] constexpr Scope<T> CreateScope(Args&&... args)
	{
		return std::make_unique<T>(std::forward<Args>(args)...);
	}

	// Shared ownership
	template <typename T>
	using Ref = std::shared_ptr<T>;

	template <typename T, typename... Args>
	[[nodiscard]] Ref<T> CreateRef(Args&&... args)
	{
		return std::make_shared<T>(std::forward<Args>(args)...);
	}

}
