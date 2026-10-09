#include "Lodestone/Core/CommandLine.h"

#include <doctest/doctest.h>

#include <array>
#include <string>
#include <vector>

namespace Lodestone {

	namespace {

		constexpr std::array<std::string_view, 2> Flags = {"--version", "--headless"};
		constexpr std::array<std::string_view, 1> Options = {"--frames"};

	}

	TEST_CASE("CommandLine skips the executable's name")
	{
		const std::array<const char*, 3> argv = {"Lodestone", "--version", "--frames=3"};

		const CommandLine commandLine(static_cast<int>(argv.size()), argv.data());

		CHECK(commandLine.GetArguments() == std::vector<std::string>{"--version", "--frames=3"});
	}

	TEST_CASE("CommandLine finds flags")
	{
		const CommandLine commandLine({"--headless"});

		CHECK(commandLine.HasFlag("--headless"));
		CHECK_FALSE(commandLine.HasFlag("--version"));
	}

	TEST_CASE("CommandLine reads option values in both forms")
	{
		CHECK(CommandLine({"--frames", "10"}).GetValue("--frames") == "10");
		CHECK(CommandLine({"--frames=12"}).GetValue("--frames") == "12");
		CHECK(CommandLine({"--frames="}).GetValue("--frames") == "");
		CHECK_FALSE(CommandLine({"--version"}).GetValue("--frames").has_value());
	}

	TEST_CASE("CommandLine parses whole-number option values")
	{
		const auto given = CommandLine({"--frames", "42"}).GetUnsignedValue("--frames");
		REQUIRE(given.has_value());
		CHECK(*given == std::optional<uint32_t>(42));

		const auto absent = CommandLine({}).GetUnsignedValue("--frames");
		REQUIRE(absent.has_value());
		CHECK_FALSE(absent->has_value());

		for (const char* invalid : {"-1", "ten", "4x", "", "99999999999"})
		{
			CAPTURE(invalid);
			const auto value = CommandLine({"--frames", invalid}).GetUnsignedValue("--frames");
			REQUIRE_FALSE(value.has_value());
			CHECK(value.error().GetCode() == ErrorCode::InvalidArgument);
		}
	}

	TEST_CASE("CommandLine::Validate accepts known flags and options")
	{
		CHECK(CommandLine({"--version", "--frames", "3", "--headless", "--frames=4"})
				.Validate(Flags, Options)
				.has_value());
		CHECK(CommandLine({}).Validate(Flags, Options).has_value());
	}

	TEST_CASE("CommandLine::Validate rejects unknown arguments and options without values")
	{
		const auto unknown = CommandLine({"--verbose"}).Validate(Flags, Options);
		REQUIRE_FALSE(unknown.has_value());
		CHECK(unknown.error().GetMessageText() == "Unknown argument '--verbose'");

		const auto missingValue = CommandLine({"--frames"}).Validate(Flags, Options);
		REQUIRE_FALSE(missingValue.has_value());
		CHECK(missingValue.error().GetMessageText() == "Option '--frames' needs a value");

		// A flag doesn't take a value
		CHECK_FALSE(CommandLine({"--version=1"}).Validate(Flags, Options).has_value());
	}

}
