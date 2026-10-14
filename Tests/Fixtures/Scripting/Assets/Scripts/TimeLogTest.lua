-- Time, the log, and print

local Check = require("Scripts.Check")

---@class TimeLogTest : Behavior
local TimeLogTest = {}

---@param dt number
function TimeLogTest:OnUpdate(dt)
	Check.Near(Time.Delta, 1 / 60, "Time.Delta")
	Check.Equal(dt, Time.Delta, "OnUpdate's dt")
	if Time.Tick == 2 then
		Check.Near(Time.Elapsed, 2 / 60, "Time.Elapsed")
		Log.Info("info from a script", 1, true)
		Log.Warn("a warning from a script", vec3(1, 2, 3))
		Log.Error("an error from a script")
		print("print from a script")
		self.Entity:SetName("Passed")
	end
end

return TimeLogTest
