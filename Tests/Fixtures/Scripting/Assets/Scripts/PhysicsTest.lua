-- Physics: raycasts, impulses, sleeping, and collision and trigger callbacks. The test's scene has a floor whose top
-- is at height 0. A ball dropped through a trigger lands on it; its script records the callbacks it gets

local Check = require("Scripts.Check")

---@class PhysicsTest : Behavior
---@field BallScript string
---@field Floor Entity
---@field Ball Entity
---@field Zone Entity
---@field Pushed Entity
local PhysicsTest = {Properties = {BallScript = ""}}

function PhysicsTest:OnCreate()
	local floor = Scene.Find("Floor")
	if floor == nil then
		error("no Floor")
	end
	self.Floor = floor

	local hit = Physics.Raycast(vec3(0, 10, 0), vec3(0, -1, 0))
	if hit == nil then
		error("the ray hit nothing")
	end
	Check.True(hit.Entity == floor, "Physics.Raycast hits the floor")
	Check.Near(hit.Point, vec3(0, 0, 0), "where the ray hit")
	Check.Near(hit.Normal, vec3(0, 1, 0), "the normal where the ray hit")
	Check.Near(hit.Distance, 10, "how far the ray went")
	Check.Equal(Physics.Raycast(vec3(0, 10, 0), vec3(0, 1, 0)), nil, "a ray that hits nothing")
	Check.Equal(Physics.Raycast(vec3(0, 10, 0), vec3(0, -1, 0), {MaxDistance = 5}), nil, "a ray too short to hit")
	Check.Equal(Physics.Raycast(vec3(0, 10, 0), vec3(0, -1, 0), {Ignore = floor}), nil, "a ray ignoring the floor")
	-- Rays and their options are checked
	Check.Fails(function() Physics.Raycast(vec3(0, 10, 0), vec3()) end, "direction", "a ray without a direction")
	Check.Fails(function() Physics.Raycast(vec3(0, 10, 0), vec3(0, -1, 0), {MaxDistance = -1}) end,
		"MaxDistance is a distance over 0", "a negative distance")
	---@diagnostic disable-next-line: assign-type-mismatch
	Check.Fails(function() Physics.RaycastAll(vec3(0, 10, 0), vec3(0, -1, 0), {IncludeTriggers = 1}) end,
		"IncludeTriggers is a boolean", "an option of the wrong type")
	Check.Fails(function() Physics.Raycast(vec3(0, 10, 0), vec3(0, -1, 0), {Maxdistance = 5}) end,
		"options are MaxDistance, IncludeTriggers and Ignore", "an unknown option")

	-- A trigger, and a ball above it
	local zone = Scene.Create("Zone")
	zone:Get("Transform").Position = vec3(0, 3, 0)
	zone:Add("RigidBody", {Type = 0, IsTrigger = true})
	zone:Add("BoxCollider", {Size = vec3(4, 1, 4)})
	local ball = Scene.Create("Ball")
	ball:Get("Transform").Position = vec3(0, 6, 0)
	ball:Add("RigidBody")
	ball:Add("SphereCollider", {Radius = 0.5})
	-- The test gives the recording script's asset as a property
	ball:Add("Script", {Script = self.BallScript})
	self.Ball = ball
	self.Zone = zone
	-- Floating, so only the impulse moves it
	self.Pushed = Scene.Create("Pushed")
	self.Pushed:Get("Transform").Position = vec3(10, 5, 0)
	self.Pushed:Add("RigidBody", {LinearDamping = 0, GravityFactor = 0})
	self.Pushed:Add("BoxCollider")
	Check.Fails(function() Physics.AddImpulse(zone, vec3(1, 0, 0)) end, "isn't a dynamic body", "an impulse on a static body")
end

function PhysicsTest:OnUpdate()
	if Time.Tick == 1 then
		-- The bodies made last tick are in the world now
		local hits = Physics.RaycastAll(vec3(0, 10, 0), vec3(0, -1, 0), {IncludeTriggers = true})
		Check.Equal(#hits, 3, "Physics.RaycastAll hits the ball, the trigger and the floor")
		Check.True(hits[1].Entity == self.Ball and hits[2].Entity == self.Zone and hits[3].Entity == self.Floor, "nearest first")
		-- One newton second on a kilogram: one meter per second
		Physics.AddImpulse(self.Pushed, vec3(0, 0, -1))
	elseif Time.Tick == 3 then
		Check.Near(self.Pushed:Get("RigidBody").LinearVelocity, vec3(0, 0, -1), "Physics.AddImpulse")
	elseif Time.Tick == 300 then
		local ballScript = self.Ball:GetScript() --[[@as BallRecorder?]]
		if ballScript == nil then
			error("the ball has no script")
		end
		Check.Equal(ballScript.Entered, 1, "OnTriggerEnter")
		Check.Equal(ballScript.Exited, 1, "OnTriggerExit")
		Check.Equal(ballScript.Landed, 1, "OnCollisionBegin")
		Check.True(Physics.IsSleeping(self.Ball), "Physics.IsSleeping")
		-- Lifted off the floor, the ball stops touching it
		self.Ball:Get("Transform").Position = vec3(0, 20, 0)
	elseif Time.Tick == 302 then
		local ballScript = self.Ball:GetScript() --[[@as BallRecorder]]
		Check.Equal(ballScript.LeftFloor, 1, "OnCollisionEnd")
		Check.True(not Physics.IsSleeping(self.Ball), "a body falling isn't asleep")
		self.Entity:SetName("Passed")
	end
end

return PhysicsTest
