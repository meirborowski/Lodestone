#pragma once

#include "Lodestone/Core/Error.h"

#include <spdlog/fmt/fmt.h>

#include <expected>
#include <string>

namespace Lodestone::Testing {

	// The error of a failed result as text, or nothing if it succeeded. For assertion messages, which doctest also
	// evaluates when the assertion passes (with -s), so they mustn't call error() on a successful result:
	//   REQUIRE_MESSAGE(scene.has_value(), Testing::DescribeError(scene));
	template <typename T>
	std::string DescribeError(const std::expected<T, Error>& result)
	{
		return result.has_value() ? std::string() : fmt::format("{}", result.error());
	}

}
