#include "Lodestone/Input/DeviceInput.h"

#include "Lodestone/Core/Assert.h"

#include <utility>

namespace Lodestone {

	namespace {

		bool WasButtonPressed(const GamepadState& current, const GamepadState& previous, GamepadButton button)
		{
			const auto index = std::to_underlying(button);
			return current.Buttons[index] && !previous.Buttons[index];
		}

	}

	void DeviceInput::BeginFrame()
	{
		m_KeysPressed.reset();
		m_KeysReleased.reset();
		m_MouseButtonsPressed.reset();
		m_MouseButtonsReleased.reset();
		m_MouseDelta = glm::vec2(0.0f);
		m_ScrollDelta = glm::vec2(0.0f);
		m_Text.clear();
		m_PreviousGamepads = m_Gamepads;
	}

	void DeviceInput::OnKey(Key key, bool down)
	{
		const auto index = std::to_underlying(key);
		LS_CORE_ASSERT(index < KeyCodeCount, "Invalid key code {}", index);
		if (down == m_KeysDown[index])
			return;

		m_KeysDown[index] = down;
		if (down)
			m_KeysPressed[index] = true;
		else
			m_KeysReleased[index] = true;
	}

	void DeviceInput::OnMouseButton(MouseButton button, bool down)
	{
		const auto index = std::to_underlying(button);
		LS_CORE_ASSERT(index < MouseButtonCount, "Invalid mouse button {}", index);
		if (down == m_MouseButtonsDown[index])
			return;

		m_MouseButtonsDown[index] = down;
		if (down)
			m_MouseButtonsPressed[index] = true;
		else
			m_MouseButtonsReleased[index] = true;
	}

	void DeviceInput::OnCursorMoved(glm::vec2 position)
	{
		// The first position only establishes where the cursor is; it isn't movement
		if (m_HasMousePosition)
			m_MouseDelta += position - m_MousePosition;
		m_MousePosition = position;
		m_HasMousePosition = true;
	}

	void DeviceInput::OnScroll(glm::vec2 offset)
	{
		m_ScrollDelta += offset;
	}

	void DeviceInput::OnText(char32_t codepoint)
	{
		m_Text.push_back(codepoint);
	}

	void DeviceInput::ReleaseAll()
	{
		m_KeysReleased |= m_KeysDown;
		m_KeysDown.reset();
		m_MouseButtonsReleased |= m_MouseButtonsDown;
		m_MouseButtonsDown.reset();
	}

	void DeviceInput::SetGamepadState(uint32_t index, const GamepadState& state)
	{
		LS_CORE_ASSERT(index < MaxGamepads, "Invalid gamepad index {}", index);
		m_Gamepads[index] = state;
	}

	bool DeviceInput::IsKeyDown(Key key) const
	{
		return m_KeysDown[std::to_underlying(key)];
	}

	bool DeviceInput::WasKeyPressed(Key key) const
	{
		return m_KeysPressed[std::to_underlying(key)];
	}

	bool DeviceInput::WasKeyReleased(Key key) const
	{
		return m_KeysReleased[std::to_underlying(key)];
	}

	bool DeviceInput::IsMouseButtonDown(MouseButton button) const
	{
		return m_MouseButtonsDown[std::to_underlying(button)];
	}

	bool DeviceInput::WasMouseButtonPressed(MouseButton button) const
	{
		return m_MouseButtonsPressed[std::to_underlying(button)];
	}

	bool DeviceInput::WasMouseButtonReleased(MouseButton button) const
	{
		return m_MouseButtonsReleased[std::to_underlying(button)];
	}

	bool DeviceInput::IsGamepadConnected(uint32_t index) const
	{
		return index < MaxGamepads && m_Gamepads[index].Connected;
	}

	const std::string& DeviceInput::GetGamepadName(uint32_t index) const
	{
		LS_CORE_ASSERT(index < MaxGamepads, "Invalid gamepad index {}", index);
		return m_Gamepads[index].Name;
	}

	bool DeviceInput::IsGamepadButtonDown(uint32_t index, GamepadButton button) const
	{
		return IsGamepadConnected(index) && m_Gamepads[index].Buttons[std::to_underlying(button)];
	}

	bool DeviceInput::WasGamepadButtonPressed(uint32_t index, GamepadButton button) const
	{
		return IsGamepadConnected(index) && WasButtonPressed(m_Gamepads[index], m_PreviousGamepads[index], button);
	}

	bool DeviceInput::WasGamepadButtonReleased(uint32_t index, GamepadButton button) const
	{
		return index < MaxGamepads && WasButtonPressed(m_PreviousGamepads[index], m_Gamepads[index], button);
	}

	float DeviceInput::GetGamepadAxis(uint32_t index, GamepadAxis axis) const
	{
		return IsGamepadConnected(index) ? m_Gamepads[index].Axes[std::to_underlying(axis)] : 0.0f;
	}

}
