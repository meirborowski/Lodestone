-- Input, over the ticks the test sends: on tick 0, W, Space and the left mouse button go down, the mouse moves and
-- scrolls, and gamepad A goes down with the left stick to the right, all for player 0; player 1's Up arrow is held.
-- On tick 1, W stays down and the rest is let go

local Check = require("Scripts.Check")

---@class InputTest : Behavior
local InputTest = {}

function InputTest:OnUpdate()
	if Time.Tick == 0 then
		Check.True(Input.IsKeyDown("W"), "Input.IsKeyDown")
		Check.True(Input.WasKeyPressed("Space"), "Input.WasKeyPressed")
		Check.True(not Input.WasKeyReleased("Space"), "Input.WasKeyReleased before it's let go")
		Check.True(Input.IsMouseButtonDown("Left"), "Input.IsMouseButtonDown")
		Check.True(Input.WasMouseButtonPressed("Left"), "Input.WasMouseButtonPressed")
		Check.Equal(Input.GetMousePosition(), vec2(100, 200), "Input.GetMousePosition")
		Check.Equal(Input.GetMouseDelta(), vec2(3, 4), "Input.GetMouseDelta")
		Check.Equal(Input.GetScrollDelta(), vec2(0, 1), "Input.GetScrollDelta")
		Check.True(Input.IsGamepadConnected(), "Input.IsGamepadConnected")
		Check.True(Input.IsGamepadButtonDown("A"), "Input.IsGamepadButtonDown")
		Check.True(Input.WasGamepadButtonPressed("A"), "Input.WasGamepadButtonPressed")
		Check.Equal(Input.GetGamepadAxis("LeftX"), 0.5, "Input.GetGamepadAxis")
		-- Another player's input
		Check.True(Input.IsKeyDown("Up", 1), "a second player's key")
		Check.True(not Input.IsKeyDown("W", 1), "a second player's input is their own")
		Check.True(not Input.IsGamepadConnected(1), "a second player without a gamepad")
		-- Names are checked (the annotations know them too, so these calls are marked as meant)
		---@diagnostic disable: param-type-mismatch
		Check.Fails(function() Input.IsKeyDown("w") end, "no key called 'w'", "an unknown key")
		Check.Fails(function() Input.IsMouseButtonDown("Middle Button") end, "no mouse button", "an unknown button")
		Check.Fails(function() Input.IsGamepadButtonDown("Z") end, "no gamepad button", "an unknown gamepad button")
		Check.Fails(function() Input.GetGamepadAxis("Stick") end, "no gamepad axis", "an unknown axis")
		---@diagnostic enable: param-type-mismatch
	elseif Time.Tick == 1 then
		Check.True(Input.IsKeyDown("W") and not Input.WasKeyPressed("W"), "a held key")
		Check.True(Input.WasKeyReleased("Space"), "Input.WasKeyReleased")
		Check.True(Input.WasMouseButtonReleased("Left"), "Input.WasMouseButtonReleased")
		Check.True(Input.WasGamepadButtonReleased("A"), "Input.WasGamepadButtonReleased")
		Check.Equal(Input.GetMouseDelta(), vec2(), "no mouse movement")
		self.Entity:SetName("Passed")
	end
end

return InputTest
