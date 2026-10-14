-- Mistakes the script type check must find, each marked with the problem it should report: the test of the check
-- itself (tools/check-lua.py --expect-marked). Uses the test scripts' annotations (Tests/Fixtures/Scripting)

---@class Mistakes : Behavior
local Mistakes = {}

function Mistakes:OnCreate()
	local entity = Scene.Fnd("Player") -- expect: undefined-field
	local player = Scene.Create("Player")
	player:SetName(42) -- expect: param-type-mismatch
	player:Get("Transfrom") -- expect: param-type-mismatch
	local body = player:Add("RigidBody")
	body.Mass = "heavy" -- expect: assign-type-mismatch
	body.Weight = 2 -- expect: inject-field
	local _ = Input.IsKeyDown("Enter ") -- expect: param-type-mismatch
	local _ = vec3(1, 2, 3):Lenght() -- expect: undefined-field
	io.write("files are out of reach") -- expect: undefined-global
	Log.Info(entity)
end

return Mistakes
