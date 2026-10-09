#pragma once

#include "Lodestone/Core/Error.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Lodestone {

	// An executable's command-line arguments: flags ("--version") and options with values ("--frames 10" or
	// "--frames=10")
	class CommandLine
	{
	public:
		// The arguments after the executable's name
		CommandLine(int argc, const char* const* argv);
		explicit CommandLine(std::vector<std::string> arguments);

		// Checks that every argument is one of the given flags or options, and that every option has a value
		[[nodiscard]] std::expected<void, Error> Validate(
			std::span<const std::string_view> flags, std::span<const std::string_view> options) const;

		bool HasFlag(std::string_view name) const;
		// The value of an option, or nothing if it wasn't given
		std::optional<std::string_view> GetValue(std::string_view name) const;
		// The value of an option that must be a whole number, or nothing if it wasn't given
		[[nodiscard]] std::expected<std::optional<uint32_t>, Error> GetUnsignedValue(std::string_view name) const;

		const std::vector<std::string>& GetArguments() const { return m_Arguments; }

	private:
		std::vector<std::string> m_Arguments;
	};

}
