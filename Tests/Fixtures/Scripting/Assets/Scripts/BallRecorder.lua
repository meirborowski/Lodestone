-- Counts the collision and trigger callbacks its entity gets, for PhysicsTest.lua

---@class BallRecorder : Behavior
---@field Entered integer
---@field Exited integer
---@field Landed integer
---@field LeftFloor integer
local BallRecorder = {}

function BallRecorder:OnCreate()
	self.Entered = 0
	self.Exited = 0
	self.Landed = 0
	self.LeftFloor = 0
end

---@param other Entity
function BallRecorder:OnTriggerEnter(other)
	if other:GetName() == "Zone" then
		self.Entered = self.Entered + 1
	end
end

---@param other Entity
function BallRecorder:OnTriggerExit(other)
	if other:GetName() == "Zone" then
		self.Exited = self.Exited + 1
	end
end

---@param other Entity
function BallRecorder:OnCollisionBegin(other)
	if other:GetName() == "Floor" then
		self.Landed = self.Landed + 1
	end
end

---@param other Entity
function BallRecorder:OnCollisionEnd(other)
	if other:GetName() == "Floor" then
		self.LeftFloor = self.LeftFloor + 1
	end
end

return BallRecorder
