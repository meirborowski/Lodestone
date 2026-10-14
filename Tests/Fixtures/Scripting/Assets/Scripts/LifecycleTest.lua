-- A script's life: properties, OnCreate, OnUpdate, OnDestroy and OnReload, and scripts calling each other. The test
-- gives Speed, Target and the helper's script asset, and reloads the script after tick 3

local Check = require("Scripts.Check")

---@class LifecycleTest : Behavior
---@field Speed number
---@field Label string
---@field Target vec3
---@field Updates integer
---@field Reloads integer
---@field HelperScript string
---@field Helper Entity
local LifecycleTest = {Properties = {Speed = 1, Label = "default", Target = vec3(0, 0, 0), HelperScript = ""}}

function LifecycleTest:OnCreate()
	Check.Equal(self.Speed, 4, "a property the Script component overrides")
	Check.Equal(self.Label, "default", "a property it doesn't")
	Check.Equal(self.Target, vec3(1, 2, 3), "a vector property")
	self.Updates = 0
	self.Reloads = 0
	-- A helper entity running a script of its own, which this one talks to
	local helper = Scene.Create("Helper")
	helper:Add("Script", {Script = self.HelperScript})
	self.Helper = helper
end

function LifecycleTest:OnUpdate()
	self.Updates = self.Updates + 1
	if Time.Tick == 2 then
		local helper = self.Helper:GetScript() --[[@as Helper?]]
		if helper == nil then
			error("the helper's script hasn't started")
		end
		Check.Equal(helper:Greet("world"), "hello world", "calling another entity's script")
		Check.Equal(helper.Created, true, "the helper's OnCreate ran")
		self.Helper:Destroy()
	elseif Time.Tick == 4 then
		Check.Equal(self.Reloads, 1, "OnReload")
		Check.Equal(self.Updates, 5, "the instance kept its fields through the reload")
		self.Entity:SetName("Passed")
	end
end

function LifecycleTest:OnReload()
	self.Reloads = self.Reloads + 1
end

function LifecycleTest:OnDestroy()
	Log.Warn("LifecycleTest ended")
end

return LifecycleTest
