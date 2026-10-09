#include "Lodestone/Input/InputCodes.h"

#include <array>
#include <utility>

namespace Lodestone {

	namespace {

		struct KeyName
		{
			Key Code;
			std::string_view Name;
		};

		constexpr std::array KeyNames = {
			KeyName{.Code = Key::Space, .Name = "Space"},
			KeyName{.Code = Key::Apostrophe, .Name = "Apostrophe"},
			KeyName{.Code = Key::Comma, .Name = "Comma"},
			KeyName{.Code = Key::Minus, .Name = "Minus"},
			KeyName{.Code = Key::Period, .Name = "Period"},
			KeyName{.Code = Key::Slash, .Name = "Slash"},
			KeyName{.Code = Key::D0, .Name = "D0"},
			KeyName{.Code = Key::D1, .Name = "D1"},
			KeyName{.Code = Key::D2, .Name = "D2"},
			KeyName{.Code = Key::D3, .Name = "D3"},
			KeyName{.Code = Key::D4, .Name = "D4"},
			KeyName{.Code = Key::D5, .Name = "D5"},
			KeyName{.Code = Key::D6, .Name = "D6"},
			KeyName{.Code = Key::D7, .Name = "D7"},
			KeyName{.Code = Key::D8, .Name = "D8"},
			KeyName{.Code = Key::D9, .Name = "D9"},
			KeyName{.Code = Key::Semicolon, .Name = "Semicolon"},
			KeyName{.Code = Key::Equal, .Name = "Equal"},
			KeyName{.Code = Key::A, .Name = "A"},
			KeyName{.Code = Key::B, .Name = "B"},
			KeyName{.Code = Key::C, .Name = "C"},
			KeyName{.Code = Key::D, .Name = "D"},
			KeyName{.Code = Key::E, .Name = "E"},
			KeyName{.Code = Key::F, .Name = "F"},
			KeyName{.Code = Key::G, .Name = "G"},
			KeyName{.Code = Key::H, .Name = "H"},
			KeyName{.Code = Key::I, .Name = "I"},
			KeyName{.Code = Key::J, .Name = "J"},
			KeyName{.Code = Key::K, .Name = "K"},
			KeyName{.Code = Key::L, .Name = "L"},
			KeyName{.Code = Key::M, .Name = "M"},
			KeyName{.Code = Key::N, .Name = "N"},
			KeyName{.Code = Key::O, .Name = "O"},
			KeyName{.Code = Key::P, .Name = "P"},
			KeyName{.Code = Key::Q, .Name = "Q"},
			KeyName{.Code = Key::R, .Name = "R"},
			KeyName{.Code = Key::S, .Name = "S"},
			KeyName{.Code = Key::T, .Name = "T"},
			KeyName{.Code = Key::U, .Name = "U"},
			KeyName{.Code = Key::V, .Name = "V"},
			KeyName{.Code = Key::W, .Name = "W"},
			KeyName{.Code = Key::X, .Name = "X"},
			KeyName{.Code = Key::Y, .Name = "Y"},
			KeyName{.Code = Key::Z, .Name = "Z"},
			KeyName{.Code = Key::LeftBracket, .Name = "LeftBracket"},
			KeyName{.Code = Key::Backslash, .Name = "Backslash"},
			KeyName{.Code = Key::RightBracket, .Name = "RightBracket"},
			KeyName{.Code = Key::GraveAccent, .Name = "GraveAccent"},
			KeyName{.Code = Key::World1, .Name = "World1"},
			KeyName{.Code = Key::World2, .Name = "World2"},
			KeyName{.Code = Key::Escape, .Name = "Escape"},
			KeyName{.Code = Key::Enter, .Name = "Enter"},
			KeyName{.Code = Key::Tab, .Name = "Tab"},
			KeyName{.Code = Key::Backspace, .Name = "Backspace"},
			KeyName{.Code = Key::Insert, .Name = "Insert"},
			KeyName{.Code = Key::Delete, .Name = "Delete"},
			KeyName{.Code = Key::Right, .Name = "Right"},
			KeyName{.Code = Key::Left, .Name = "Left"},
			KeyName{.Code = Key::Down, .Name = "Down"},
			KeyName{.Code = Key::Up, .Name = "Up"},
			KeyName{.Code = Key::PageUp, .Name = "PageUp"},
			KeyName{.Code = Key::PageDown, .Name = "PageDown"},
			KeyName{.Code = Key::Home, .Name = "Home"},
			KeyName{.Code = Key::End, .Name = "End"},
			KeyName{.Code = Key::CapsLock, .Name = "CapsLock"},
			KeyName{.Code = Key::ScrollLock, .Name = "ScrollLock"},
			KeyName{.Code = Key::NumLock, .Name = "NumLock"},
			KeyName{.Code = Key::PrintScreen, .Name = "PrintScreen"},
			KeyName{.Code = Key::Pause, .Name = "Pause"},
			KeyName{.Code = Key::F1, .Name = "F1"},
			KeyName{.Code = Key::F2, .Name = "F2"},
			KeyName{.Code = Key::F3, .Name = "F3"},
			KeyName{.Code = Key::F4, .Name = "F4"},
			KeyName{.Code = Key::F5, .Name = "F5"},
			KeyName{.Code = Key::F6, .Name = "F6"},
			KeyName{.Code = Key::F7, .Name = "F7"},
			KeyName{.Code = Key::F8, .Name = "F8"},
			KeyName{.Code = Key::F9, .Name = "F9"},
			KeyName{.Code = Key::F10, .Name = "F10"},
			KeyName{.Code = Key::F11, .Name = "F11"},
			KeyName{.Code = Key::F12, .Name = "F12"},
			KeyName{.Code = Key::F13, .Name = "F13"},
			KeyName{.Code = Key::F14, .Name = "F14"},
			KeyName{.Code = Key::F15, .Name = "F15"},
			KeyName{.Code = Key::F16, .Name = "F16"},
			KeyName{.Code = Key::F17, .Name = "F17"},
			KeyName{.Code = Key::F18, .Name = "F18"},
			KeyName{.Code = Key::F19, .Name = "F19"},
			KeyName{.Code = Key::F20, .Name = "F20"},
			KeyName{.Code = Key::F21, .Name = "F21"},
			KeyName{.Code = Key::F22, .Name = "F22"},
			KeyName{.Code = Key::F23, .Name = "F23"},
			KeyName{.Code = Key::F24, .Name = "F24"},
			KeyName{.Code = Key::F25, .Name = "F25"},
			KeyName{.Code = Key::Keypad0, .Name = "Keypad0"},
			KeyName{.Code = Key::Keypad1, .Name = "Keypad1"},
			KeyName{.Code = Key::Keypad2, .Name = "Keypad2"},
			KeyName{.Code = Key::Keypad3, .Name = "Keypad3"},
			KeyName{.Code = Key::Keypad4, .Name = "Keypad4"},
			KeyName{.Code = Key::Keypad5, .Name = "Keypad5"},
			KeyName{.Code = Key::Keypad6, .Name = "Keypad6"},
			KeyName{.Code = Key::Keypad7, .Name = "Keypad7"},
			KeyName{.Code = Key::Keypad8, .Name = "Keypad8"},
			KeyName{.Code = Key::Keypad9, .Name = "Keypad9"},
			KeyName{.Code = Key::KeypadDecimal, .Name = "KeypadDecimal"},
			KeyName{.Code = Key::KeypadDivide, .Name = "KeypadDivide"},
			KeyName{.Code = Key::KeypadMultiply, .Name = "KeypadMultiply"},
			KeyName{.Code = Key::KeypadSubtract, .Name = "KeypadSubtract"},
			KeyName{.Code = Key::KeypadAdd, .Name = "KeypadAdd"},
			KeyName{.Code = Key::KeypadEnter, .Name = "KeypadEnter"},
			KeyName{.Code = Key::KeypadEqual, .Name = "KeypadEqual"},
			KeyName{.Code = Key::LeftShift, .Name = "LeftShift"},
			KeyName{.Code = Key::LeftControl, .Name = "LeftControl"},
			KeyName{.Code = Key::LeftAlt, .Name = "LeftAlt"},
			KeyName{.Code = Key::LeftSuper, .Name = "LeftSuper"},
			KeyName{.Code = Key::RightShift, .Name = "RightShift"},
			KeyName{.Code = Key::RightControl, .Name = "RightControl"},
			KeyName{.Code = Key::RightAlt, .Name = "RightAlt"},
			KeyName{.Code = Key::RightSuper, .Name = "RightSuper"},
			KeyName{.Code = Key::Menu, .Name = "Menu"},
		};

		// The name of each key code, empty for codes that aren't keys
		constexpr std::array<std::string_view, KeyCodeCount> KeyNamesByCode = []
		{
			std::array<std::string_view, KeyCodeCount> names{};
			for (const KeyName& key : KeyNames)
				names[std::to_underlying(key.Code)] = key.Name;
			return names;
		}();

		constexpr std::array<std::string_view, MouseButtonCount> MouseButtonNames = {
			"Left", "Right", "Middle", "Button4", "Button5", "Button6", "Button7", "Button8"};

		constexpr std::array<std::string_view, GamepadButtonCount> GamepadButtonNames = {"A", "B", "X", "Y",
			"LeftBumper", "RightBumper", "Back", "Start", "Guide", "LeftThumb", "RightThumb", "DpadUp", "DpadRight",
			"DpadDown", "DpadLeft"};

		constexpr std::array<std::string_view, GamepadAxisCount> GamepadAxisNames = {
			"LeftX", "LeftY", "RightX", "RightY", "LeftTrigger", "RightTrigger"};

	}

	std::optional<Key> KeyFromCode(int code)
	{
		if (code < 0 || std::cmp_greater_equal(code, KeyCodeCount) || KeyNamesByCode[static_cast<size_t>(code)].empty())
			return std::nullopt;
		return static_cast<Key>(code);
	}

	std::string_view ToString(Key key)
	{
		const auto code = std::to_underlying(key);
		if (code >= KeyCodeCount || KeyNamesByCode[code].empty())
			return "Unknown";
		return KeyNamesByCode[code];
	}

	std::optional<Key> KeyFromName(std::string_view name)
	{
		for (uint32_t code = 0; code < KeyCodeCount; ++code)
		{
			if (!KeyNamesByCode[code].empty() && KeyNamesByCode[code] == name)
				return static_cast<Key>(code);
		}
		return std::nullopt;
	}

	std::optional<MouseButton> MouseButtonFromName(std::string_view name)
	{
		for (uint32_t index = 0; index < MouseButtonCount; ++index)
		{
			if (MouseButtonNames[index] == name)
				return static_cast<MouseButton>(index);
		}
		return std::nullopt;
	}

	std::string_view ToString(MouseButton button)
	{
		const auto index = std::to_underlying(button);
		return index < MouseButtonNames.size() ? MouseButtonNames[index] : "Unknown";
	}

	std::string_view ToString(GamepadButton button)
	{
		const auto index = std::to_underlying(button);
		return index < GamepadButtonNames.size() ? GamepadButtonNames[index] : "Unknown";
	}

	std::string_view ToString(GamepadAxis axis)
	{
		const auto index = std::to_underlying(axis);
		return index < GamepadAxisNames.size() ? GamepadAxisNames[index] : "Unknown";
	}

}
