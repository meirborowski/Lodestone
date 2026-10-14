-- A goal: a trigger behind a paddle. The ball reaching it scores a point for the other side

---@class Goal : Behavior
---@field Scorer "Left"|"Right" Who scores when the ball gets here
local Goal = {Properties = {Scorer = "Right"}}

---@param other Entity
function Goal:OnTriggerEnter(other)
	if other:GetName() ~= "Ball" then
		return
	end
	local game = Scene.Find("Game")
	local script = game and game:GetScript() --[[@as Game?]]
	if script ~= nil then
		script:Score(self.Scorer)
	end
end

return Goal
