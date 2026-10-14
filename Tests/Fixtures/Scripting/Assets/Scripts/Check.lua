-- Assertions for the scripting API's test scripts (Tests/Core/ScriptingApiTests.cpp), loaded with require. A failed
-- check raises an error, which the test reports with the script and line

local Check = {}

---Fails unless the condition holds
---@param condition any
---@param message string
function Check.True(condition, message)
	if not condition then
		error(message, 2)
	end
end

---Fails unless the values are equal
---@param actual any
---@param expected any
---@param what string
function Check.Equal(actual, expected, what)
	if actual ~= expected then
		error(string.format("%s: expected %s, got %s", what, tostring(expected), tostring(actual)), 2)
	end
end

---Fails unless two numbers, or the components of two vectors, are within a tolerance of each other
---@param actual number|vec2|vec3|vec4|quat
---@param expected number|vec2|vec3|vec4|quat
---@param what string
---@param tolerance? number
function Check.Near(actual, expected, what, tolerance)
	tolerance = tolerance or 1e-4
	local function near(a, b)
		return math.abs(a - b) <= tolerance
	end
	local same
	if type(actual) == "number" then
		same = near(actual, expected)
	else
		same = near(actual.x, expected.x) and near(actual.y, expected.y)
		if actual.z ~= nil then
			same = same and near(actual.z, expected.z)
		end
		if actual.w ~= nil then
			same = same and near(actual.w, expected.w)
		end
	end
	if not same then
		error(string.format("%s: expected %s, got %s", what, tostring(expected), tostring(actual)), 2)
	end
end

---Fails unless calling the function raises an error whose message contains the text
---@param call fun(): any
---@param text string
---@param what string
function Check.Fails(call, text, what)
	local succeeded, message = pcall(call)
	if succeeded then
		error(what .. ": expected an error", 2)
	end
	if not string.find(tostring(message), text, 1, true) then
		error(string.format("%s: expected an error saying '%s', got '%s'", what, text, tostring(message)), 2)
	end
end

return Check
