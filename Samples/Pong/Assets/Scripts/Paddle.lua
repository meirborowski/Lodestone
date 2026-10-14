-- A paddle, sliding along the field's length (Z): moved by its player's keys, or by the computer, following the ball

---@class Paddle : Behavior
---@field Up KeyName The key that moves it toward the top of the screen (-Z)
---@field Down KeyName
---@field Player integer Whose input moves it
---@field Computer boolean Whether the computer plays it instead
---@field Speed number In meters per second
---@field ComputerSpeed number How fast the computer moves it: slower than the ball can go, so it can be beaten
---@field Reach number How far its center goes from the middle of the field
---@field Ball Entity?
---@field Aim number? Where on the paddle the computer means to hit the ball, from its center
local Paddle = {
	Properties = {Up = "W", Down = "S", Player = 0, Computer = false, Speed = 9, ComputerSpeed = 5.5, Reach = 4},
}

function Paddle:OnCreate()
	self.Ball = Scene.Find("Ball")
end

---@param dt number
function Paddle:OnUpdate(dt)
	local transform = self.Entity:Get("Transform")
	if transform == nil then
		return
	end
	local position = transform.Position
	local direction = 0
	local speed = self.Speed
	if self.Computer then
		-- Toward the ball while it comes this way, meaning to hit it off center so it goes back at an angle; back to
		-- the middle while it goes away
		speed = self.ComputerSpeed
		local ball = self.Ball
		local body = ball and ball:Get("RigidBody")
		if ball ~= nil and body ~= nil then
			local target = 0
			if body.LinearVelocity.x * (position.x - ball:GetWorldPosition().x) > 0 then
				if self.Aim == nil then
					self.Aim = (math.random() * 2 - 1) * 0.7 * transform.Scale.z / 2
				end
				target = ball:GetWorldPosition().z - self.Aim
			else
				self.Aim = nil
			end
			if target > position.z + 0.1 then
				direction = 1
			elseif target < position.z - 0.1 then
				direction = -1
			end
		end
	else
		if Input.IsKeyDown(self.Up, self.Player) then
			direction = direction - 1
		end
		if Input.IsKeyDown(self.Down, self.Player) then
			direction = direction + 1
		end
	end
	position.z = math.max(-self.Reach, math.min(self.Reach, position.z + direction * speed * dt))
	transform.Position = position
end

return Paddle
