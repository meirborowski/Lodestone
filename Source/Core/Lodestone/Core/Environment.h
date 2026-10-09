#pragma once

#include "Lodestone/Core/Error.h"

#include <expected>
#include <optional>
#include <string>
#include <string_view>

// Named Read/WriteEnvironmentVariable rather than Get/SetEnvironmentVariable, which <windows.h> defines as macros

namespace Lodestone {

	// The value of an environment variable, or nothing if it isn't set. Don't call it while another thread changes the
	// environment
	std::optional<std::string> ReadEnvironmentVariable(std::string_view name);

	// Sets an environment variable for this process and the processes it starts. Don't call it while another thread
	// reads or changes the environment
	[[nodiscard]] std::expected<void, Error> WriteEnvironmentVariable(std::string_view name, std::string_view value);

}
