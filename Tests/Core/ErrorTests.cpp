#include "Lodestone/Core/Error.h"

#include <doctest/doctest.h>
#include <spdlog/fmt/fmt.h>

#include <expected>
#include <string>
#include <utility>

namespace Lodestone {

	namespace {

		std::expected<int, Error> ParsePositive(int value)
		{
			if (value <= 0)
				return std::unexpected(Error(ErrorCode::InvalidArgument, fmt::format("{} isn't positive", value)));
			return value;
		}

	}

	TEST_CASE("An error keeps its code and message")
	{
		const Error error(ErrorCode::FileNotFound, "Textures/Hero.png");

		CHECK(error.GetCode() == ErrorCode::FileNotFound);
		CHECK(error.GetMessageText() == "Textures/Hero.png");
	}

	TEST_CASE("Error::ToString includes the code and the message")
	{
		CHECK(Error(ErrorCode::ParseError, "Unexpected '}' at line 3").ToString() ==
			"ParseError: Unexpected '}' at line 3");
		CHECK(Error(ErrorCode::OutOfMemory).ToString() == "OutOfMemory");
	}

	TEST_CASE("WithContext prepends context to the message")
	{
		const Error original(ErrorCode::FileNotFound, "Hero.png");

		SUBCASE("From an lvalue, leaving the original unchanged")
		{
			const Error withContext = original.WithContext("Loading material 'Hero'");
			CHECK(withContext.GetCode() == ErrorCode::FileNotFound);
			CHECK(withContext.GetMessageText() == "Loading material 'Hero': Hero.png");
			CHECK(original.GetMessageText() == "Hero.png");
		}

		SUBCASE("From an rvalue")
		{
			Error error = original;
			const Error withContext = std::move(error).WithContext("Loading scene 'Level1'");
			CHECK(withContext.GetMessageText() == "Loading scene 'Level1': Hero.png");
		}

		SUBCASE("Repeatedly, outermost context first")
		{
			const Error withContext = original.WithContext("Loading material").WithContext("Loading scene");
			CHECK(withContext.GetMessageText() == "Loading scene: Loading material: Hero.png");
		}

		SUBCASE("To an error without a message")
		{
			CHECK(
				Error(ErrorCode::OutOfMemory).WithContext("Allocating a mesh").GetMessageText() == "Allocating a mesh");
		}

		SUBCASE("Empty context changes nothing")
		{
			CHECK(original.WithContext("") == original);
		}
	}

	TEST_CASE("Errors compare by code and message")
	{
		CHECK(Error(ErrorCode::NotFound, "a") == Error(ErrorCode::NotFound, "a"));
		CHECK(Error(ErrorCode::NotFound, "a") != Error(ErrorCode::NotFound, "b"));
		CHECK(Error(ErrorCode::NotFound, "a") != Error(ErrorCode::AlreadyExists, "a"));
	}

	TEST_CASE("Every error code has a name")
	{
		CHECK(ToString(ErrorCode::Unknown) == "Unknown");
		CHECK(ToString(ErrorCode::InvalidArgument) == "InvalidArgument");
		CHECK(ToString(ErrorCode::InvalidState) == "InvalidState");
		CHECK(ToString(ErrorCode::NotFound) == "NotFound");
		CHECK(ToString(ErrorCode::AlreadyExists) == "AlreadyExists");
		CHECK(ToString(ErrorCode::FileNotFound) == "FileNotFound");
		CHECK(ToString(ErrorCode::IoError) == "IoError");
		CHECK(ToString(ErrorCode::ParseError) == "ParseError");
		CHECK(ToString(ErrorCode::UnsupportedVersion) == "UnsupportedVersion");
		CHECK(ToString(ErrorCode::Unsupported) == "Unsupported");
		CHECK(ToString(ErrorCode::OutOfMemory) == "OutOfMemory");
		CHECK(ToString(ErrorCode::ScriptError) == "ScriptError");
		CHECK(ToString(ErrorCode::DeviceError) == "DeviceError");
	}

	TEST_CASE("Errors and error codes can be formatted for logging")
	{
		CHECK(fmt::format("{}", ErrorCode::IoError) == "IoError");
		CHECK(fmt::format("Failed: {}", Error(ErrorCode::IoError, "Disk full")) == "Failed: IoError: Disk full");
	}

	TEST_CASE("Errors are returned through std::expected")
	{
		const std::expected<int, Error> success = ParsePositive(4);
		REQUIRE(success.has_value());
		CHECK(*success == 4);

		const std::expected<int, Error> failure = ParsePositive(-2);
		REQUIRE_FALSE(failure.has_value());
		CHECK(failure.error().GetCode() == ErrorCode::InvalidArgument);
		CHECK(failure.error().GetMessageText() == "-2 isn't positive");

		const std::expected<int, Error> withContext = ParsePositive(0).transform_error(
			[](const Error& error) { return error.WithContext("Reading the tick rate"); });
		REQUIRE_FALSE(withContext.has_value());
		CHECK(withContext.error().GetMessageText() == "Reading the tick rate: 0 isn't positive");
	}

}
