-- Entities and their components: names, components through reflection, the hierarchy, world transforms and scripts

local Check = require("Scripts.Check")

---@class EntityTest : Behavior
local EntityTest = {}

function EntityTest:OnCreate()
	local entity = Scene.Create("Thing")
	Check.True(entity:IsValid(), "a new entity is valid")
	Check.Equal(#entity:GetId(), 36, "an entity's UUID as text")
	Check.Equal(entity:GetName(), "Thing", "Entity:GetName")
	entity:SetName("Renamed")
	Check.Equal(entity:GetName(), "Renamed", "Entity:SetName")
	Check.True(entity == Scene.Get(entity:GetId()), "entities compare by identity")
	Check.True(string.find(tostring(entity), "Renamed", 1, true) ~= nil, "tostring(entity)")

	-- Components, through reflection
	Check.True(entity:Has("Transform"), "every entity has a Transform")
	Check.True(not entity:Has("PointLight"), "Entity:Has without the component")
	Check.Equal(entity:Get("PointLight"), nil, "Entity:Get without the component")
	local transform = entity:Get("Transform")
	if transform == nil then
		error("no Transform")
	end
	transform.Position = vec3(1, 2, 3)
	transform.Scale = vec3(2)
	Check.Equal(transform.Position, vec3(1, 2, 3), "a vec3 field")
	local light = entity:Add("PointLight", {Intensity = 50, Range = 4})
	Check.Equal(light.Intensity, 50, "a field set when the component is added")
	light.Color = vec3(1, 0.5, 0)
	Check.Equal(light.Color, vec3(1, 0.5, 0), "a field set on a component")
	Check.Equal(entity:Get("Name").Name, "Renamed", "a string field")
	Check.Equal(entity:Get("ID").ID, entity:GetId(), "a UUID field, as text")
	Check.Fails(function() entity:Get("ID").ID = "x" end, "read-only", "a read-only field")
	Check.Fails(function() light.Intensity = -1 end, "Intensity", "a value outside the field's limits")
	---@diagnostic disable-next-line: assign-type-mismatch
	Check.Fails(function() light.Intensity = "bright" end, "PointLight.Intensity", "a value of the wrong type")
	---@diagnostic disable-next-line: undefined-field
	Check.Fails(function() return light.Brightness end, "no field 'Brightness'", "an unknown field")
	Check.Fails(function() entity:Add("PointLight") end, "already has", "adding a component twice")
	---@diagnostic disable-next-line: param-type-mismatch
	Check.Fails(function() entity:Add("Nothing") end, "no component called 'Nothing'", "an unknown component")
	Check.Fails(function() entity:Add("SpotLight", {Range = "far"}) end, "SpotLight.Range", "a bad field when adding")
	Check.True(not entity:Has("SpotLight"), "a component with a bad field isn't added")
	Check.Fails(function() entity:Remove("Transform") end, "needs its Transform", "removing a required component")
	---@diagnostic disable-next-line: param-type-mismatch
	Check.Fails(function() entity:Add("PreviousTransform") end, "no component called", "internal components are hidden")
	Check.True(entity:Remove("PointLight"), "Entity:Remove")
	Check.True(not entity:Remove("PointLight"), "removing a component that's gone")
	Check.Fails(function() return light.Intensity end, "no longer has a PointLight", "a removed component's fields")

	-- The hierarchy, and world transforms
	local parent = Scene.Create("Parent")
	parent:Get("Transform").Position = vec3(10, 0, 0)
	parent:Get("Transform").Rotation = quat.AngleAxis(90, vec3(0, 1, 0))
	entity:SetParent(parent)
	Check.True(entity:GetParent() == parent, "Entity:GetParent")
	Check.Equal(#parent:GetChildren(), 1, "Entity:GetChildren")
	Check.True(parent:FindChild("Renamed") == entity, "Entity:FindChild")
	Check.Equal(parent:FindChild("Nobody"), nil, "Entity:FindChild without one")
	Check.Near(entity:GetWorldPosition(), vec3(13, 2, -1), "Entity:GetWorldPosition")
	Check.Near(entity:GetWorldRotation() * vec3(0, 0, -1), vec3(-1, 0, 0), "Entity:GetWorldRotation")
	Check.Near(entity:GetForward(), vec3(-1, 0, 0), "Entity:GetForward")
	Check.Fails(function() parent:SetParent(entity) end, "", "parenting an entity under its own child")
	entity:SetParent(nil)
	Check.Equal(entity:GetParent(), nil, "back at the root")

	-- Scripts: an entity's instance, and its fields
	Check.True(self.Entity:GetScript() == self, "Entity:GetScript")
	Check.Equal(entity:GetScript(), nil, "an entity without a script")

	-- Destroyed entities are no longer valid, and fail cleanly
	parent:Destroy()
	Check.True(not parent:IsValid(), "Entity:Destroy")
	Check.Fails(function() parent:GetName() end, "doesn't exist", "a destroyed entity")
	Check.True(string.find(tostring(parent), "destroyed", 1, true) ~= nil, "tostring of a destroyed entity")

	self.Entity:SetName("Passed")
end

return EntityTest
