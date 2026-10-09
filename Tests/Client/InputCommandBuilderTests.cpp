#include "Lodestone/Input/InputCommandBuilder.h"

#include <doctest/doctest.h>

namespace Lodestone {

	TEST_CASE("A command carries the tick, the player and what's held")
	{
		DeviceInput input;
		InputCommandBuilder builder(2);
		input.OnKey(Key::W, true);
		input.OnMouseButton(MouseButton::Right, true);
		input.OnCursorMoved(glm::vec2(10.0f, 20.0f));
		builder.AccumulateFrame(input);

		const InputCommand command = builder.BuildCommand(42);

		CHECK(command.Tick == 42);
		CHECK(command.Player == 2);
		CHECK(command.IsKeyDown(Key::W));
		CHECK(command.WasKeyPressed(Key::W));
		CHECK(command.IsMouseButtonDown(MouseButton::Right));
		CHECK(command.MousePosition == glm::vec2(10.0f, 20.0f));
	}

	TEST_CASE("Held keys carry over to later ticks; transitions and movement don't")
	{
		DeviceInput input;
		InputCommandBuilder builder;
		input.OnKey(Key::W, true);
		input.OnCursorMoved(glm::vec2(0.0f));
		input.OnCursorMoved(glm::vec2(3.0f, 4.0f));
		input.OnScroll(glm::vec2(0.0f, 1.0f));
		builder.AccumulateFrame(input);

		const InputCommand first = builder.BuildCommand(0);
		const InputCommand second = builder.BuildCommand(1);

		CHECK(first.MouseDelta == glm::vec2(3.0f, 4.0f));
		CHECK(first.ScrollDelta == glm::vec2(0.0f, 1.0f));
		CHECK(second.IsKeyDown(Key::W));
		CHECK_FALSE(second.WasKeyPressed(Key::W));
		CHECK(second.MouseDelta == glm::vec2(0.0f));
		CHECK(second.ScrollDelta == glm::vec2(0.0f));
	}

	TEST_CASE("A key tapped between two ticks is pressed and released in the next command")
	{
		DeviceInput input;
		InputCommandBuilder builder;

		// Two frames before the tick: the key goes down in the first and up in the second
		input.BeginFrame();
		input.OnKey(Key::Space, true);
		builder.AccumulateFrame(input);
		input.BeginFrame();
		input.OnKey(Key::Space, false);
		input.OnCursorMoved(glm::vec2(1.0f));
		input.OnCursorMoved(glm::vec2(2.0f));
		builder.AccumulateFrame(input);

		const InputCommand command = builder.BuildCommand(0);
		CHECK(command.WasKeyPressed(Key::Space));
		CHECK(command.WasKeyReleased(Key::Space));
		CHECK_FALSE(command.IsKeyDown(Key::Space));
	}

	TEST_CASE("Movement from several frames adds up in one command")
	{
		DeviceInput input;
		InputCommandBuilder builder;
		input.OnCursorMoved(glm::vec2(0.0f));
		for (int frame = 0; frame < 3; ++frame)
		{
			input.BeginFrame();
			input.OnScroll(glm::vec2(1.0f, 0.0f));
			builder.AccumulateFrame(input);
		}
		CHECK(builder.BuildCommand(0).ScrollDelta == glm::vec2(3.0f, 0.0f));
	}

	TEST_CASE("The player's gamepad goes into its commands")
	{
		DeviceInput input;
		InputCommandBuilder builder(0, 1);
		GamepadState pad;
		pad.Connected = true;
		pad.Buttons[std::to_underlying(GamepadButton::A)] = true;
		pad.Axes[std::to_underlying(GamepadAxis::LeftY)] = 0.75f;
		input.BeginFrame();
		input.SetGamepadState(1, pad);
		builder.AccumulateFrame(input);

		const InputCommand command = builder.BuildCommand(0);
		CHECK(command.GamepadConnected);
		CHECK(command.IsGamepadButtonDown(GamepadButton::A));
		CHECK(command.WasGamepadButtonPressed(GamepadButton::A));
		CHECK(command.GetGamepadAxis(GamepadAxis::LeftY) == 0.75f);

		// Another player's builder without a gamepad sees none
		InputCommandBuilder keyboardOnly(1, std::nullopt);
		keyboardOnly.AccumulateFrame(input);
		const InputCommand other = keyboardOnly.BuildCommand(0);
		CHECK_FALSE(other.GamepadConnected);
		CHECK_FALSE(other.IsGamepadButtonDown(GamepadButton::A));
	}

}
