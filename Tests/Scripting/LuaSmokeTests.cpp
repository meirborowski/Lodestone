// Smoke tests for Lua 5.4 and sol2, as Lodestone builds and configures them (see cmake/Dependencies.cmake and
// docs/Decisions/0003-lua-compiled-as-cpp.md). The scripting runtime itself arrives in Milestone 8.

#include <doctest/doctest.h>
#include <sol/sol.hpp>

#include <stdexcept>
#include <string>

namespace Lodestone {

	namespace {

		struct Counter
		{
			void Add(int amount) { Value += amount; }

			int Value = 0;
		};

		// Records its own destruction, to check that stack unwinding ran
		class DestructionProbe
		{
		public:
			explicit DestructionProbe(bool* destroyed)
				: m_Destroyed(destroyed)
			{
			}

			~DestructionProbe() { *m_Destroyed = true; }

			DestructionProbe(const DestructionProbe&) = delete;
			DestructionProbe& operator=(const DestructionProbe&) = delete;
			DestructionProbe(DestructionProbe&&) = delete;
			DestructionProbe& operator=(DestructionProbe&&) = delete;

		private:
			bool* m_Destroyed;
		};

		std::string GetErrorMessage(const sol::protected_function_result& result)
		{
			const sol::error error = result;
			return error.what();
		}

	}

	TEST_CASE("Lua is version 5.4")
	{
		CHECK(LUA_VERSION_NUM == 504);

		sol::state lua;
		lua.open_libraries(sol::lib::base);
		CHECK(lua.get<std::string>("_VERSION") == "Lua 5.4");
	}

	TEST_CASE("sol2 binds C++ types and functions to Lua")
	{
		sol::state lua;
		lua.open_libraries(sol::lib::base);
		lua.new_usertype<Counter>("Counter", "Value", &Counter::Value, "Add", &Counter::Add);
		lua.set_function("Square", [](double value) { return value * value; });

		const sol::protected_function_result result = lua.safe_script(R"(
			local counter = Counter.new()
			counter:Add(40)
			counter:Add(2)
			return counter.Value, Square(1.5)
		)",
			sol::script_pass_on_error);

		REQUIRE(result.valid());
		CHECK(result.get<int>(0) == 42);
		CHECK(result.get<double>(1) == doctest::Approx(2.25));
	}

	TEST_CASE("Lua errors are reported with the script name and line")
	{
		sol::state lua;
		lua.open_libraries(sol::lib::base);

		const auto syntaxError =
			lua.safe_script("local a = 1\nlocal b = = 2", sol::script_pass_on_error, "@Broken.lua");
		REQUIRE_FALSE(syntaxError.valid());
		CHECK(GetErrorMessage(syntaxError).contains("Broken.lua:2:"));

		const auto runtimeError = lua.safe_script("\n\nerror('boom')", sol::script_pass_on_error, "@Failing.lua");
		REQUIRE_FALSE(runtimeError.valid());
		CHECK(GetErrorMessage(runtimeError).contains("Failing.lua:3: boom"));
	}

	TEST_CASE("Wrong argument types passed to a binding become Lua errors")
	{
		sol::state lua;
		lua.open_libraries(sol::lib::base);
		lua.set_function("Square", [](double value) { return value * value; });

		const auto result = lua.safe_script("return Square({})", sol::script_pass_on_error);

		REQUIRE_FALSE(result.valid());
	}

	TEST_CASE("A C++ exception thrown by a binding becomes a Lua error with its message")
	{
		sol::state lua;
		lua.open_libraries(sol::lib::base);
		lua.set_function("Fail", []() { throw std::runtime_error("binding failed"); });

		const auto result = lua.safe_script("Fail()", sol::script_pass_on_error);

		REQUIRE_FALSE(result.valid());
		CHECK(GetErrorMessage(result).contains("binding failed"));
	}

	TEST_CASE("A Lua error raised through a C++ binding runs the binding's destructors")
	{
		// Lua is compiled as C++, so a Lua error unwinds C++ stack frames as an exception. With Lua compiled as C, it
		// would longjmp over them without running their destructors
		sol::state lua;
		lua.open_libraries(sol::lib::base);
		bool destroyed = false;
		lua.set_function("CallWithProbe",
			[&destroyed](const sol::unsafe_function& callback)
			{
				const DestructionProbe probe(&destroyed);
				callback();
			});

		const auto result =
			lua.safe_script("CallWithProbe(function() error('raised in Lua') end)", sol::script_pass_on_error);

		REQUIRE_FALSE(result.valid());
		CHECK(GetErrorMessage(result).contains("raised in Lua"));
		CHECK(destroyed);
	}

}
