-- The match: serves, the score - shown as a row of markers on each side - and the end, when a side reaches the winning
-- score. Space starts a new match. The Status entity's name says what's happening, for whoever's watching the scene

---@class Game : Behavior
---@field WinningScore integer
---@field ServeDelay number Seconds between a point and the next serve
---@field MarkerSpacing number Meters between score markers
---@field Scores table<"Left"|"Right", integer>
---@field Winner ("Left"|"Right")?
---@field ServeTo ("Left"|"Right")?
---@field ServeIn number
---@field Ball Entity
---@field Status Entity
local Game = {Properties = {WinningScore = 5, ServeDelay = 1, MarkerSpacing = 0.6}}

local Opponent = {Left = "Right", Right = "Left"}

---Something the game can't do without, which mustn't be nil
---@generic T
---@param value T?
---@param name string
---@return T
local function Require(value, name)
	if value == nil then
		error("The scene has no " .. name)
	end
	return value
end

function Game:OnCreate()
	self.Ball = Require(Scene.Find("Ball"), "Ball")
	self.Status = Require(Scene.Find("Status"), "Status")
	self:Restart()
end

---@return Ball
function Game:GetBall()
	return Require(self.Ball:GetScript(), "Ball script") --[[@as Ball]]
end

---Starts a new match: no points, and a serve to the player
function Game:Restart()
	self.Scores = {Left = 0, Right = 0}
	self.Winner = nil
	for _, side in ipairs({"Left", "Right"}) do
		local board = Require(Scene.Find(side .. " Score"), side .. " Score")
		for _, marker in ipairs(board:GetChildren()) do
			marker:Destroy()
		end
	end
	self:WaitToServe("Left")
	Log.Info("A new match")
end

---@param side "Left"|"Right"
function Game:WaitToServe(side)
	self:GetBall():Stop()
	self.ServeTo = side
	self.ServeIn = self.ServeDelay
	self:ShowStatus()
end

function Game:ShowStatus()
	local score = string.format("%d - %d", self.Scores.Left, self.Scores.Right)
	if self.Winner ~= nil then
		self.Status:SetName(string.format("%s wins %s - Space to play again", self.Winner, score))
	elseif self.ServeTo ~= nil then
		self.Status:SetName("Serving " .. score)
	else
		self.Status:SetName("Playing " .. score)
	end
end

---A point for a side: a marker on its board, then the next serve - to the side that lost it - or the end
---@param side "Left"|"Right"
function Game:Score(side)
	if self.Winner ~= nil or self.ServeTo ~= nil then
		return
	end
	self.Scores[side] = self.Scores[side] + 1
	local board = Scene.Find(side .. " Score")
	local marker = Scene.Instantiate("Prefabs/ScoreMarker.lprefab", board)
	local direction = side == "Left" and -1 or 1
	marker:Get("Transform").Position = vec3(direction * (self.Scores[side] - 1) * self.MarkerSpacing, 0, 0)
	Log.Info(string.format("%s scores: %d - %d", side, self.Scores.Left, self.Scores.Right))

	if self.Scores[side] >= self.WinningScore then
		self.Winner = side
		self.ServeTo = nil
		self:GetBall():Stop()
		self:ShowStatus()
		Log.Info(side .. " wins")
	else
		self:WaitToServe(Opponent[side])
	end
end

---@param dt number
function Game:OnUpdate(dt)
	if self.Winner ~= nil then
		if Input.WasKeyPressed("Space") then
			self:Restart()
		end
		return
	end
	if self.ServeTo ~= nil then
		self.ServeIn = self.ServeIn - dt
		if self.ServeIn <= 0 then
			self:GetBall():Serve(self.ServeTo)
			self.ServeTo = nil
			self:ShowStatus()
		end
	end
end

return Game
