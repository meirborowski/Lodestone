#include "Lodestone/Input/InputCodes.h"

#include <doctest/doctest.h>

#include <utility>

namespace Lodestone {

	TEST_CASE("Key codes convert from GLFW's values")
	{
		CHECK(KeyFromCode(32) == Key::Space);
		CHECK(KeyFromCode(65) == Key::A);
		CHECK(KeyFromCode(256) == Key::Escape);
		CHECK(KeyFromCode(348) == Key::Menu);
	}

	TEST_CASE("Codes that aren't keys don't convert")
	{
		// GLFW_KEY_UNKNOWN, the gaps between key ranges, and out-of-range values
		for (const int code : {-1, 0, 31, 33, 58, 60, 97, 255, 285, 349, 1000})
		{
			CAPTURE(code);
			CHECK_FALSE(KeyFromCode(code).has_value());
		}
	}

	TEST_CASE("Every key code converts to its key and has a name")
	{
		int keyCount = 0;
		for (int code = 0; std::cmp_less(code, KeyCodeCount); ++code)
		{
			const std::optional<Key> key = KeyFromCode(code);
			if (!key)
				continue;
			CAPTURE(code);
			CHECK(std::to_underlying(*key) == code);
			CHECK(ToString(*key) != "Unknown");
			++keyCount;
		}
		// Every key GLFW defines
		CHECK(keyCount == 120);
	}

	TEST_CASE("Input codes have readable names")
	{
		CHECK(ToString(Key::D7) == "D7");
		CHECK(ToString(Key::KeypadEnter) == "KeypadEnter");
		CHECK(ToString(Key::LeftControl) == "LeftControl");
		CHECK(ToString(MouseButton::Middle) == "Middle");
		CHECK(ToString(MouseButton::Button8) == "Button8");
		CHECK(ToString(GamepadButton::DpadLeft) == "DpadLeft");
		CHECK(ToString(GamepadAxis::RightTrigger) == "RightTrigger");
	}

}
