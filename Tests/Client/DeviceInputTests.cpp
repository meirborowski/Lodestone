#include "Lodestone/Input/DeviceInput.h"

#include <doctest/doctest.h>

namespace Lodestone {

	TEST_CASE("A key press is reported for the frame it happens in")
	{
		DeviceInput input;

		input.BeginFrame();
		input.OnKey(Key::W, true);
		CHECK(input.IsKeyDown(Key::W));
		CHECK(input.WasKeyPressed(Key::W));
		CHECK_FALSE(input.WasKeyReleased(Key::W));

		input.BeginFrame();
		CHECK(input.IsKeyDown(Key::W));
		CHECK_FALSE(input.WasKeyPressed(Key::W));

		input.OnKey(Key::W, false);
		CHECK_FALSE(input.IsKeyDown(Key::W));
		CHECK(input.WasKeyReleased(Key::W));
	}

	TEST_CASE("A key tapped within one frame reports both the press and the release")
	{
		DeviceInput input;
		input.BeginFrame();

		input.OnKey(Key::Space, true);
		input.OnKey(Key::Space, false);

		CHECK_FALSE(input.IsKeyDown(Key::Space));
		CHECK(input.WasKeyPressed(Key::Space));
		CHECK(input.WasKeyReleased(Key::Space));
	}

	TEST_CASE("A repeated key-down event isn't a new press")
	{
		DeviceInput input;
		input.BeginFrame();
		input.OnKey(Key::A, true);
		input.BeginFrame();

		input.OnKey(Key::A, true);

		CHECK(input.IsKeyDown(Key::A));
		CHECK_FALSE(input.WasKeyPressed(Key::A));
	}

	TEST_CASE("Mouse buttons report presses and releases")
	{
		DeviceInput input;
		input.BeginFrame();

		input.OnMouseButton(MouseButton::Left, true);
		CHECK(input.IsMouseButtonDown(MouseButton::Left));
		CHECK(input.WasMouseButtonPressed(MouseButton::Left));

		input.BeginFrame();
		input.OnMouseButton(MouseButton::Left, false);
		CHECK_FALSE(input.IsMouseButtonDown(MouseButton::Left));
		CHECK(input.WasMouseButtonReleased(MouseButton::Left));
		CHECK_FALSE(input.IsMouseButtonDown(MouseButton::Right));
	}

	TEST_CASE("Mouse movement and scrolling accumulate over a frame")
	{
		DeviceInput input;
		input.BeginFrame();

		// The first position establishes where the cursor is, and isn't movement
		input.OnCursorMoved({100.0f, 50.0f});
		CHECK(input.GetMouseDelta() == glm::vec2(0.0f));

		input.OnCursorMoved({110.0f, 45.0f});
		input.OnCursorMoved({115.0f, 40.0f});
		input.OnScroll({0.0f, 1.0f});
		input.OnScroll({0.0f, 2.0f});
		CHECK(input.GetMousePosition() == glm::vec2(115.0f, 40.0f));
		CHECK(input.GetMouseDelta() == glm::vec2(15.0f, -10.0f));
		CHECK(input.GetScrollDelta() == glm::vec2(0.0f, 3.0f));

		input.BeginFrame();
		CHECK(input.GetMousePosition() == glm::vec2(115.0f, 40.0f));
		CHECK(input.GetMouseDelta() == glm::vec2(0.0f));
		CHECK(input.GetScrollDelta() == glm::vec2(0.0f));
	}

	TEST_CASE("Text typed during a frame is collected in order")
	{
		DeviceInput input;
		input.BeginFrame();

		input.OnText(U'h');
		input.OnText(U'é');
		CHECK(input.GetText() == U"hé");

		input.BeginFrame();
		CHECK(input.GetText().empty());
	}

	TEST_CASE("ReleaseAll releases every held key and mouse button")
	{
		DeviceInput input;
		input.BeginFrame();
		input.OnKey(Key::LeftShift, true);
		input.OnMouseButton(MouseButton::Right, true);
		input.BeginFrame();

		input.ReleaseAll();

		CHECK_FALSE(input.IsKeyDown(Key::LeftShift));
		CHECK(input.WasKeyReleased(Key::LeftShift));
		CHECK_FALSE(input.IsMouseButtonDown(MouseButton::Right));
		CHECK(input.WasMouseButtonReleased(MouseButton::Right));
		CHECK_FALSE(input.WasKeyReleased(Key::A));
	}

	TEST_CASE("Gamepad buttons report presses and releases between polls")
	{
		DeviceInput input;
		GamepadState state;
		state.Connected = true;
		state.Name = "Test pad";

		input.BeginFrame();
		input.SetGamepadState(1, state);
		CHECK(input.IsGamepadConnected(1));
		CHECK_FALSE(input.IsGamepadConnected(0));
		CHECK(input.GetGamepadName(1) == "Test pad");

		input.BeginFrame();
		state.Buttons[static_cast<size_t>(GamepadButton::A)] = true;
		state.Axes[static_cast<size_t>(GamepadAxis::LeftX)] = -0.5f;
		input.SetGamepadState(1, state);
		CHECK(input.IsGamepadButtonDown(1, GamepadButton::A));
		CHECK(input.WasGamepadButtonPressed(1, GamepadButton::A));
		CHECK(input.GetGamepadAxis(1, GamepadAxis::LeftX) == doctest::Approx(-0.5f));

		input.BeginFrame();
		input.SetGamepadState(1, state);
		CHECK(input.IsGamepadButtonDown(1, GamepadButton::A));
		CHECK_FALSE(input.WasGamepadButtonPressed(1, GamepadButton::A));

		input.BeginFrame();
		state.Buttons[static_cast<size_t>(GamepadButton::A)] = false;
		input.SetGamepadState(1, state);
		CHECK(input.WasGamepadButtonReleased(1, GamepadButton::A));
	}

	TEST_CASE("A disconnected gamepad reads as idle")
	{
		DeviceInput input;
		GamepadState state;
		state.Connected = true;
		state.Buttons[static_cast<size_t>(GamepadButton::Start)] = true;
		state.Axes[static_cast<size_t>(GamepadAxis::RightY)] = 1.0f;
		input.BeginFrame();
		input.SetGamepadState(0, state);

		input.BeginFrame();
		input.SetGamepadState(0, GamepadState{});

		CHECK_FALSE(input.IsGamepadConnected(0));
		CHECK_FALSE(input.IsGamepadButtonDown(0, GamepadButton::Start));
		CHECK(input.WasGamepadButtonReleased(0, GamepadButton::Start));
		CHECK(input.GetGamepadAxis(0, GamepadAxis::RightY) == 0.0f);
		CHECK_FALSE(input.IsGamepadConnected(MaxGamepads));
	}

}
