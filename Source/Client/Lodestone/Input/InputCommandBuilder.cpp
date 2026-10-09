#include "Lodestone/Input/InputCommandBuilder.h"

#include "Lodestone/Core/Assert.h"

namespace Lodestone {

	InputCommandBuilder::InputCommandBuilder(uint32_t player, std::optional<uint32_t> gamepad)
		: m_Player(player), m_Gamepad(gamepad)
	{
		LS_CORE_ASSERT(!m_Gamepad || *m_Gamepad < MaxGamepads, "Invalid gamepad index {}", *m_Gamepad);
	}

	void InputCommandBuilder::AccumulateFrame(const DeviceInput& input)
	{
		m_Pending.KeysDown = input.GetKeysDown();
		m_Pending.KeysPressed |= input.GetKeysPressed();
		m_Pending.KeysReleased |= input.GetKeysReleased();

		m_Pending.MouseButtonsDown = input.GetMouseButtonsDown();
		m_Pending.MouseButtonsPressed |= input.GetMouseButtonsPressed();
		m_Pending.MouseButtonsReleased |= input.GetMouseButtonsReleased();
		m_Pending.MousePosition = input.GetMousePosition();
		m_Pending.MouseDelta += input.GetMouseDelta();
		m_Pending.ScrollDelta += input.GetScrollDelta();

		m_Pending.GamepadConnected = m_Gamepad && input.IsGamepadConnected(*m_Gamepad);
		if (!m_Pending.GamepadConnected)
		{
			m_Pending.GamepadButtonsDown.reset();
			m_Pending.GamepadAxes.fill(0.0f);
			return;
		}
		for (uint32_t index = 0; index < GamepadButtonCount; ++index)
		{
			const auto button = static_cast<GamepadButton>(index);
			m_Pending.GamepadButtonsDown[index] = input.IsGamepadButtonDown(*m_Gamepad, button);
			m_Pending.GamepadButtonsPressed[index] =
				m_Pending.GamepadButtonsPressed[index] || input.WasGamepadButtonPressed(*m_Gamepad, button);
			m_Pending.GamepadButtonsReleased[index] =
				m_Pending.GamepadButtonsReleased[index] || input.WasGamepadButtonReleased(*m_Gamepad, button);
		}
		for (uint32_t index = 0; index < GamepadAxisCount; ++index)
			m_Pending.GamepadAxes[index] = input.GetGamepadAxis(*m_Gamepad, static_cast<GamepadAxis>(index));
	}

	InputCommand InputCommandBuilder::BuildCommand(uint64_t tick)
	{
		InputCommand command = m_Pending;
		command.Tick = tick;
		command.Player = m_Player;

		// What's held carries over to the next tick; transitions and movement don't
		m_Pending.KeysPressed.reset();
		m_Pending.KeysReleased.reset();
		m_Pending.MouseButtonsPressed.reset();
		m_Pending.MouseButtonsReleased.reset();
		m_Pending.MouseDelta = glm::vec2(0.0f);
		m_Pending.ScrollDelta = glm::vec2(0.0f);
		m_Pending.GamepadButtonsPressed.reset();
		m_Pending.GamepadButtonsReleased.reset();
		return command;
	}

}
