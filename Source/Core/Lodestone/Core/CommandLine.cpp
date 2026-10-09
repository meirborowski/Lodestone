#include "Lodestone/Core/CommandLine.h"

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <charconv>
#include <utility>

namespace Lodestone {

	namespace {

		bool Contains(std::span<const std::string_view> names, std::string_view name)
		{
			return std::ranges::find(names, name) != names.end();
		}

		// For "--name=value", the name; for anything else, the whole argument
		std::string_view GetArgumentName(std::string_view argument)
		{
			const size_t separator = argument.find('=');
			return separator == std::string_view::npos ? argument : argument.substr(0, separator);
		}

	}

	CommandLine::CommandLine(int argc, const char* const* argv)
	{
		for (int i = 1; i < argc; ++i)
			m_Arguments.emplace_back(argv[i]);
	}

	CommandLine::CommandLine(std::vector<std::string> arguments)
		: m_Arguments(std::move(arguments))
	{
	}

	std::expected<void, Error> CommandLine::Validate(
		std::span<const std::string_view> flags, std::span<const std::string_view> options) const
	{
		for (size_t i = 0; i < m_Arguments.size(); ++i)
		{
			const std::string_view argument = m_Arguments[i];
			const std::string_view name = GetArgumentName(argument);
			const bool hasInlineValue = name.size() != argument.size();

			if (Contains(flags, argument))
				continue;

			if (!Contains(options, name))
				return std::unexpected(
					Error(ErrorCode::InvalidArgument, fmt::format("Unknown argument '{}'", argument)));

			if (!hasInlineValue)
			{
				if (i + 1 >= m_Arguments.size())
					return std::unexpected(
						Error(ErrorCode::InvalidArgument, fmt::format("Option '{}' needs a value", name)));
				++i;
			}
		}
		return {};
	}

	bool CommandLine::HasFlag(std::string_view name) const
	{
		return std::ranges::find(m_Arguments, name) != m_Arguments.end();
	}

	std::optional<std::string_view> CommandLine::GetValue(std::string_view name) const
	{
		for (size_t i = 0; i < m_Arguments.size(); ++i)
		{
			const std::string_view argument = m_Arguments[i];
			if (argument == name)
				return i + 1 < m_Arguments.size() ? std::optional<std::string_view>(m_Arguments[i + 1]) : std::nullopt;
			if (GetArgumentName(argument) == name)
				return argument.substr(name.size() + 1);
		}
		return std::nullopt;
	}

	std::expected<std::optional<uint32_t>, Error> CommandLine::GetUnsignedValue(std::string_view name) const
	{
		const std::optional<std::string_view> text = GetValue(name);
		if (!text)
			return std::nullopt;

		uint32_t value = 0;
		const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), value);
		if (error != std::errc() || end != text->data() + text->size())
			return std::unexpected(Error(
				ErrorCode::InvalidArgument, fmt::format("Option '{}' needs a whole number, not '{}'", name, *text)));
		return value;
	}

}
