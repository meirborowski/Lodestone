#pragma once

#include "Lodestone/Input/InputCodes.h"

#include <glm/vec2.hpp>

#include <array>
#include <bitset>
#include <cstdint>
#include <utility>

namespace Lodestone {

	// One player's input for one simulation tick. The simulation reads input only through input commands, never from
	// devices, so the same path serves local play, client input on a server, prediction replays and input sent through
	// MCP (see docs/Architecture.md#simulation).
	//
	// "Down" is the state at the end of the tick; "pressed" and "released" are transitions during it. A key tapped and
	// let go within one tick is pressed and released, but not down
	struct InputCommand
	{
		// The tick the command is for, and whose input it is
		uint64_t Tick = 0;
		uint32_t Player = 0;

		std::bitset<KeyCodeCount> KeysDown;
		std::bitset<KeyCodeCount> KeysPressed;
		std::bitset<KeyCodeCount> KeysReleased;

		std::bitset<MouseButtonCount> MouseButtonsDown;
		std::bitset<MouseButtonCount> MouseButtonsPressed;
		std::bitset<MouseButtonCount> MouseButtonsReleased;
		// In window coordinates, from the top-left corner
		glm::vec2 MousePosition{0.0f};
		// Movement and scrolling during the tick
		glm::vec2 MouseDelta{0.0f};
		glm::vec2 ScrollDelta{0.0f};

		// The player's gamepad
		bool GamepadConnected = false;
		std::bitset<GamepadButtonCount> GamepadButtonsDown;
		std::bitset<GamepadButtonCount> GamepadButtonsPressed;
		std::bitset<GamepadButtonCount> GamepadButtonsReleased;
		std::array<float, GamepadAxisCount> GamepadAxes{};

		bool IsKeyDown(Key key) const { return KeysDown[std::to_underlying(key)]; }
		bool WasKeyPressed(Key key) const { return KeysPressed[std::to_underlying(key)]; }
		bool WasKeyReleased(Key key) const { return KeysReleased[std::to_underlying(key)]; }

		bool IsMouseButtonDown(MouseButton button) const { return MouseButtonsDown[std::to_underlying(button)]; }
		bool WasMouseButtonPressed(MouseButton button) const { return MouseButtonsPressed[std::to_underlying(button)]; }
		bool WasMouseButtonReleased(MouseButton button) const
		{
			return MouseButtonsReleased[std::to_underlying(button)];
		}

		bool IsGamepadButtonDown(GamepadButton button) const { return GamepadButtonsDown[std::to_underlying(button)]; }
		bool WasGamepadButtonPressed(GamepadButton button) const
		{
			return GamepadButtonsPressed[std::to_underlying(button)];
		}
		bool WasGamepadButtonReleased(GamepadButton button) const
		{
			return GamepadButtonsReleased[std::to_underlying(button)];
		}
		float GetGamepadAxis(GamepadAxis axis) const { return GamepadAxes[std::to_underlying(axis)]; }

		bool operator==(const InputCommand& other) const = default;
	};

}
