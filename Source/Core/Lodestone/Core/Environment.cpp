#include "Lodestone/Core/Environment.h"

#include "Lodestone/Core/Base.h"

#include <spdlog/fmt/fmt.h>

#include <cstdlib>

namespace Lodestone {

	std::optional<std::string> ReadEnvironmentVariable(std::string_view name)
	{
		const std::string terminatedName(name);
#if defined(LS_PLATFORM_WINDOWS)
		// getenv is deprecated in the Microsoft C runtime
		char* value = nullptr;
		size_t size = 0;
		if (_dupenv_s(&value, &size, terminatedName.c_str()) != 0 || value == nullptr)
			return std::nullopt;
		std::string result(value);
		std::free(value);
		return result;
#else
		// Safe as long as nothing changes the environment at the same time, as documented
		const char* value = std::getenv(terminatedName.c_str()); // NOLINT(concurrency-mt-unsafe)
		if (value == nullptr)
			return std::nullopt;
		return std::string(value);
#endif
	}

	std::expected<void, Error> WriteEnvironmentVariable(std::string_view name, std::string_view value)
	{
		if (name.empty() || name.contains('='))
			return std::unexpected(
				Error(ErrorCode::InvalidArgument, fmt::format("Invalid environment variable name '{}'", name)));

		const std::string terminatedName(name);
		const std::string terminatedValue(value);
#if defined(LS_PLATFORM_WINDOWS)
		// Also updates the process environment block, which Windows APIs read
		const bool failed = _putenv_s(terminatedName.c_str(), terminatedValue.c_str()) != 0;
#else
		// Safe as long as nothing reads or changes the environment at the same time, as documented
		const bool failed =
			setenv(terminatedName.c_str(), terminatedValue.c_str(), 1) != 0; // NOLINT(concurrency-mt-unsafe)
#endif
		if (failed)
			return std::unexpected(
				Error(ErrorCode::InvalidArgument, fmt::format("Can't set environment variable {}", name)));
		return {};
	}

}
