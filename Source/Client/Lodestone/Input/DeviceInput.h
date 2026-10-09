#pragma once

#include "Lodestone/Input/InputCodes.h"

#include <glm/vec2.hpp>

#include <array>
#include <bitset>
#include <string>

namespace Lodestone {

	// The state of one gamepad, as polled from the device
	struct GamepadState
	{
		bool Connected = false;
		std::string Name;
		std::array<bool, GamepadButtonCount> Buttons{};
		std::array<float, GamepadAxisCount> Axes{};
	};

	// The keyboard, mouse and gamepads, as seen during one frame. The window system feeds it events and gamepad
	// states, and starts each frame with BeginFrame(); the client reads it.
	//
	// "Pressed" and "released" mean the transition happened during the current frame - a key tapped and released
	// within one frame reports both. The simulation never reads devices directly: it reads input commands built from
	// this state (see docs/Architecture.md#simulation).
	class DeviceInput
	{
	public:
		// Starts a new frame: clears this frame's transitions, mouse movement, scrolling and text
		void BeginFrame();

		// Events
		void OnKey(Key key, bool down);
		void OnMouseButton(MouseButton button, bool down);
		void OnCursorMoved(glm::vec2 position);
		void OnScroll(glm::vec2 offset);
		void OnText(char32_t codepoint);
		// Releases every key and mouse button, e.g. when the window loses focus and stops receiving key-up events
		void ReleaseAll();
		void SetGamepadState(uint32_t index, const GamepadState& state);

		// Keyboard
		bool IsKeyDown(Key key) const;
		bool WasKeyPressed(Key key) const;
		bool WasKeyReleased(Key key) const;

		// Mouse. Positions are in window coordinates, from the top-left corner
		bool IsMouseButtonDown(MouseButton button) const;
		bool WasMouseButtonPressed(MouseButton button) const;
		bool WasMouseButtonReleased(MouseButton button) const;
		glm::vec2 GetMousePosition() const { return m_MousePosition; }
		glm::vec2 GetMouseDelta() const { return m_MouseDelta; }
		glm::vec2 GetScrollDelta() const { return m_ScrollDelta; }

		// Text typed this frame, as Unicode code points
		const std::u32string& GetText() const { return m_Text; }

		// Gamepads, by index from 0 to MaxGamepads - 1
		bool IsGamepadConnected(uint32_t index) const;
		const std::string& GetGamepadName(uint32_t index) const;
		bool IsGamepadButtonDown(uint32_t index, GamepadButton button) const;
		bool WasGamepadButtonPressed(uint32_t index, GamepadButton button) const;
		bool WasGamepadButtonReleased(uint32_t index, GamepadButton button) const;
		float GetGamepadAxis(uint32_t index, GamepadAxis axis) const;

	private:
		std::bitset<KeyCodeCount> m_KeysDown;
		std::bitset<KeyCodeCount> m_KeysPressed;
		std::bitset<KeyCodeCount> m_KeysReleased;

		std::bitset<MouseButtonCount> m_MouseButtonsDown;
		std::bitset<MouseButtonCount> m_MouseButtonsPressed;
		std::bitset<MouseButtonCount> m_MouseButtonsReleased;
		glm::vec2 m_MousePosition{0.0f};
		glm::vec2 m_MouseDelta{0.0f};
		glm::vec2 m_ScrollDelta{0.0f};
		bool m_HasMousePosition = false;

		std::u32string m_Text;

		std::array<GamepadState, MaxGamepads> m_Gamepads;
		std::array<GamepadState, MaxGamepads> m_PreviousGamepads;
	};

}
