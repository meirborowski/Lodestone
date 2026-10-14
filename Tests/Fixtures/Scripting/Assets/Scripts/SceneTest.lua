-- The scene: creating, finding, instancing prefabs and destroying entities

local Check = require("Scripts.Check")

---@class SceneTest : Behavior
local SceneTest = {}

function SceneTest:OnCreate()
	local rootCount = #Scene.GetRoots()
	local first = Scene.Create("Crowd")
	local second = Scene.Create("Crowd")
	local child = Scene.Create("Member", first)
	Check.True(child:GetParent() == first, "Scene.Create under a parent")
	Check.Equal(Scene.Create():GetName(), "Entity", "Scene.Create without a name")
	Check.Equal(#Scene.GetRoots(), rootCount + 3, "Scene.GetRoots")
	Check.True(Scene.Find("Crowd") ~= nil, "Scene.Find")
	Check.Equal(Scene.Find("Nobody"), nil, "Scene.Find without one")
	Check.Equal(#Scene.FindAll("Crowd"), 2, "Scene.FindAll")
	Check.True(Scene.Get(second:GetId()) == second, "Scene.Get")
	Check.Equal(Scene.Get("not a UUID"), nil, "Scene.Get with something that isn't a UUID")
	second:Add("SpotLight")
	Check.Equal(#Scene.FindWith("SpotLight"), 1, "Scene.FindWith")
	---@diagnostic disable-next-line: param-type-mismatch
	Check.Fails(function() Scene.FindWith("Nothing") end, "no component called", "Scene.FindWith an unknown component")

	-- A prefab, by its path, as a root and under a parent
	local crate = Scene.Instantiate("Prefabs/Crate.lprefab")
	Check.Equal(crate:GetName(), "Crate", "Scene.Instantiate")
	Check.True(crate:Has("PrefabInstance"), "an instance records its prefab")
	Check.Equal(#crate:GetChildren(), 1, "a prefab's descendants come with it")
	local held = Scene.Instantiate("Prefabs/Crate.lprefab", first)
	Check.True(held:GetParent() == first, "Scene.Instantiate under a parent")
	Check.Fails(function() Scene.Instantiate("Prefabs/Missing.lprefab") end, "no prefab", "a missing prefab")

	Scene.Destroy(first)
	Check.True(not first:IsValid() and not child:IsValid() and not held:IsValid(), "Scene.Destroy takes descendants")
	Check.Equal(#Scene.FindAll("Crowd"), 1, "a destroyed entity isn't found")

	self.Entity:SetName("Passed")
end

return SceneTest
