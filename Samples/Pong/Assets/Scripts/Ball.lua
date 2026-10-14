-- The ball: a dynamic body without gravity or friction that bounces off the walls on its own. A paddle sends it back
-- at an angle that depends on where it hit, a little faster each time

---@class Ball : Behavior
---@field Speed number Its speed when served, in meters per second
---@field SpeedUp number How much faster each paddle hit makes it
---@field MaxSpeed number
---@field MaxAngle number The steepest a paddle sends it back, in degrees from straight across
---@field CurrentSpeed number
local Ball = {Properties = {Speed = 8, SpeedUp = 0.4, MaxSpeed = 14, MaxAngle = 55}}

---How high above the field the ball rolls
local Height = 0.5

function Ball:OnCreate()
	self.CurrentSpeed = 0
end

---Puts the ball back in the middle, at rest
function Ball:Stop()
	self.CurrentSpeed = 0
	self.Entity:Get("Transform").Position = vec3(0, Height, 0)
	self.Entity:Get("RigidBody").LinearVelocity = vec3()
end

---Sends the ball from the middle toward a side, at a random angle
---@param side "Left"|"Right"
function Ball:Serve(side)
	self.Entity:Get("Transform").Position = vec3(0, Height, 0)
	self.CurrentSpeed = self.Speed
	local angle = math.rad(math.random(-30, 30))
	local across = side == "Left" and -1 or 1
	self:Send(vec3(across * math.cos(angle), 0, math.sin(angle)))
end

---@param direction vec3
function Ball:Send(direction)
	self.Entity:Get("RigidBody").LinearVelocity = direction:Normalized() * self.CurrentSpeed
end

function Ball:OnUpdate()
	if self.CurrentSpeed == 0 then
		return
	end
	-- Bounces keep the ball's speed, but not exactly: it's kept as it should be, flat on the field
	local body = self.Entity:Get("RigidBody")
	if body == nil then
		return
	end
	local velocity = body.LinearVelocity
	velocity.y = 0
	if velocity:Length() > 0.001 then
		body.LinearVelocity = velocity:Normalized() * self.CurrentSpeed
	end
	local position = self.Entity:Get("Transform").Position
	if position.y ~= Height then
		position.y = Height
		self.Entity:Get("Transform").Position = position
	end
end

---@param other Entity
function Ball:OnCollisionBegin(other)
	if self.CurrentSpeed == 0 or not other:GetName():find("Paddle") then
		return
	end
	-- Hit near the paddle's ends, the ball goes back steeply; in its middle, straight across
	local paddle = other:GetWorldPosition()
	local ball = self.Entity:GetWorldPosition()
	local halfLength = other:Get("Transform").Scale.z / 2
	local offset = math.max(-1, math.min(1, (ball.z - paddle.z) / halfLength))
	local angle = math.rad(offset * self.MaxAngle)
	local across = ball.x > paddle.x and 1 or -1
	self.CurrentSpeed = math.min(self.MaxSpeed, self.CurrentSpeed + self.SpeedUp)
	self:Send(vec3(across * math.cos(angle), 0, math.sin(angle)))
end

return Ball
