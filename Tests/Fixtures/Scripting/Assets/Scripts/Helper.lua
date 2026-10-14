-- Another entity's script, which LifecycleTest.lua calls

---@class Helper : Behavior
---@field Created boolean
local Helper = {}

function Helper:OnCreate()
	self.Created = true
end

---@param name string
---@return string
function Helper:Greet(name)
	return "hello " .. name
end

function Helper:OnDestroy()
	Log.Warn("Helper ended")
end

return Helper
