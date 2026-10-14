-- Animation, on the test scene's skinned arm (SkinnedArm.glb: clips Bend, Wave and Sway)

local Check = require("Scripts.Check")

---@class AnimationTest : Behavior
---@field Arm Entity
local AnimationTest = {}

function AnimationTest:OnCreate()
	local arm = Scene.Find("SkinnedArm")
	if arm == nil then
		error("no arm")
	end
	self.Arm = arm
	Check.Equal(Animation.GetClipCount(arm), 3, "Animation.GetClipCount")
	Check.Equal(Animation.GetClipName(arm, 1), "Wave", "Animation.GetClipName")
	Animation.Play(arm, "Sway", {Loop = false, Speed = 2})
	Check.Equal(Animation.GetClip(arm), 2, "Animation.Play by name")
	Check.Equal(Animation.GetClipName(arm), "Sway", "the playing clip's name")
	Check.True(Animation.IsPlaying(arm), "Animation.IsPlaying")
	Check.Equal(arm:Get("Animator").Speed, 2, "Animation.Play's speed")
	Check.Equal(arm:Get("Animator").Loop, false, "Animation.Play's loop")
	-- A node of the model finds its instance's animator
	local node = Scene.Find("Upper")
	if node == nil then
		error("no Upper node")
	end
	Check.Equal(Animation.GetClip(node), 2, "an animator found from a node")
	Check.Fails(function() Animation.Play(arm, "Dance") end, "no clip called 'Dance'", "an unknown clip")
	Check.Fails(function() Animation.Play(arm, 7) end, "has 3 clips", "a clip the model doesn't have")
	Check.Fails(function() Animation.Play(self.Entity, 0) end, "has no Animator", "an entity without an animator")
	Check.Fails(function() Animation.CrossFade(arm, 0, -1) end, "0 seconds or more", "a negative crossfade")
	-- Options are checked before anything changes
	Check.Fails(function() Animation.Play(arm, 0, {Speed = 1000}) end, "-100 to 100", "a speed out of range")
	---@diagnostic disable-next-line: assign-type-mismatch
	Check.Fails(function() Animation.Play(arm, 0, {Loop = "yes"}) end, "Loop is a boolean", "a loop that isn't one")
	Check.Fails(function() Animation.Play(arm, 0, {Sped = 2}) end, "options are Loop and Speed", "an unknown option")
	Check.Equal(Animation.GetClip(arm), 2, "bad options change nothing")
end

function AnimationTest:OnUpdate()
	local arm = self.Arm
	if Time.Tick == 30 then
		-- Half a second at twice the speed
		Check.Near(Animation.GetTime(arm), 1, "Animation.GetTime", 0.05)
		Animation.Pause(arm)
		Check.True(not Animation.IsPlaying(arm), "Animation.Pause")
	elseif Time.Tick == 40 then
		Check.Near(Animation.GetTime(arm), 1, "a paused animation's time", 0.05)
		Animation.Resume(arm)
		Animation.SetSpeed(arm, 1)
		Check.Equal(arm:Get("Animator").Speed, 1, "Animation.SetSpeed")
		Check.Fails(function() Animation.SetSpeed(arm, 1000) end, "-100 to 100", "a speed out of range")
		Animation.CrossFade(arm, 0, 0.5)
		Check.Equal(Animation.GetClip(arm), 0, "Animation.CrossFade")
		Check.Near(arm:Get("Animator").FadeDuration, 0.5, "the crossfade's length")
	elseif Time.Tick == 50 then
		Animation.Stop(arm)
		Check.True(not Animation.IsPlaying(arm) and Animation.GetTime(arm) == 0, "Animation.Stop")
		self.Entity:SetName("Passed")
	end
end

return AnimationTest
