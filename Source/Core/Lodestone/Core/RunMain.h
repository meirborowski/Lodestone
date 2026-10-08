#pragma once

#include "Lodestone/Core/Log.h"

#include <functional>
#include <string_view>

namespace Lodestone {

	// Runs the body of an executable's main(): initializes the log, runs the body and shuts the log down. Returns the
	// body's exit code, or EXIT_FAILURE if the log can't be initialized or an exception escapes the body. Escaping
	// exceptions are reported and never rethrown. Every Lodestone executable's main() goes through this
	[[nodiscard]] int RunMain(
		std::string_view applicationName, const LogConfig& logConfig, const std::function<int()>& body) noexcept;

}
