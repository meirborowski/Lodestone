// Toolchain checks: the C++23 features Lodestone relies on compile and behave correctly on every supported compiler
// and standard library - GCC 14, Clang 19, MSVC and Apple Clang (see docs/TechStack.md#c-standard). When engine code
// starts using another C++23 feature, add it here.

#include <doctest/doctest.h>

#include <cstdint>
#include <expected>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <version>

// Deducing this has no check here: Clang 19 and Apple Clang 17 implement it without defining its feature-test macro
// (__cpp_explicit_this_parameter), so the Counter class below checks it by using it
static_assert(__cpp_lib_expected >= 202211L, "std::expected with monadic operations is required");
static_assert(__cpp_lib_to_underlying >= 202102L, "std::to_underlying is required");
static_assert(__cpp_lib_unreachable >= 202202L, "std::unreachable is required");
static_assert(__cpp_if_consteval >= 202106L, "if consteval is required");
static_assert(__cpp_lib_source_location >= 201907L, "std::source_location is required");
static_assert(__cpp_lib_string_contains >= 202011L, "std::string::contains is required");
static_assert(__cpp_designated_initializers >= 201707L, "Designated initializers are required");

namespace Lodestone {

	namespace {

		enum class Channel : uint8_t
		{
			Music = 3,
			Effects = 7
		};

		std::expected<int, std::string> ParseDigit(char character)
		{
			if (character < '0' || character > '9')
				return std::unexpected(std::string("not a digit"));
			return character - '0';
		}

		class Counter
		{
		public:
			// Deducing this: one definition serves const and non-const objects
			template <typename Self>
			auto& GetValue(this Self& self)
			{
				return self.m_Value;
			}

		private:
			int m_Value = 0;
		};

		constexpr int ChooseByContext()
		{
			if consteval
			{
				return 1;
			}
			else
			{
				return 2;
			}
		}

		int Classify(Channel channel)
		{
			switch (channel)
			{
				case Channel::Music:
					return 10;
				case Channel::Effects:
					return 20;
			}
			std::unreachable();
		}

		struct Settings
		{
			int Width = 0;
			int Height = 0;
			bool Fullscreen = false;
		};

	}

	TEST_CASE("std::expected and its monadic operations work")
	{
		CHECK(ParseDigit('7') == 7);
		CHECK(ParseDigit('x').error() == "not a digit");

		const auto doubled =
			ParseDigit('4').and_then([](int value) -> std::expected<int, std::string> { return value * 2; });
		CHECK(doubled == 8);

		const auto squared = ParseDigit('3').transform([](int value) { return value * value; });
		CHECK(squared == 9);

		const auto recovered =
			ParseDigit('?').or_else([](const std::string&) -> std::expected<int, std::string> { return 0; });
		CHECK(recovered == 0);

		const auto described =
			ParseDigit('?').transform_error([](const std::string& error) { return "Parsing: " + error; });
		CHECK(described.error() == "Parsing: not a digit");

		const std::expected<void, std::string> nothing;
		CHECK(nothing.has_value());
	}

	TEST_CASE("Deducing this resolves to const and non-const objects")
	{
		Counter counter;
		counter.GetValue() = 5;

		const Counter& constCounter = counter;
		CHECK(constCounter.GetValue() == 5);
		static_assert(std::is_same_v<decltype(constCounter.GetValue()), const int&>);
		static_assert(std::is_same_v<decltype(counter.GetValue()), int&>);
	}

	TEST_CASE("std::to_underlying, std::unreachable and if consteval work")
	{
		CHECK(std::to_underlying(Channel::Effects) == 7);
		CHECK(Classify(Channel::Music) == 10);

		static_assert(ChooseByContext() == 1);
		// A call through a function pointer is never constant-evaluated
		int (*const runtimeChoice)() = &ChooseByContext;
		CHECK(runtimeChoice() == 2);
	}

	TEST_CASE("std::source_location, std::string::contains and designated initializers work")
	{
		const std::source_location location = std::source_location::current();
		CHECK(std::string_view(location.file_name()).ends_with("LanguageSupportTests.cpp"));
		CHECK(location.line() > 0);

		CHECK(std::string("Lodestone").contains("stone"));

		const Settings settings{.Width = 1920, .Height = 1080};
		CHECK(settings.Width == 1920);
		CHECK_FALSE(settings.Fullscreen);
	}

}
