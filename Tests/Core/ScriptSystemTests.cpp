#include "Lodestone/Scripting/ScriptSystem.h"

#include "Common/LogCapture.h"
#include "Lodestone/Scene/Entity.h"
#include "Lodestone/Scripting/ScriptAssets.h"
#include "Lodestone/Scripting/ScriptComponents.h"
#include "Lodestone/Simulation/Simulation.h"

#include <doctest/doctest.h>

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Lodestone {

	namespace {

		// Scripts and prefabs from memory, each script with a version that changes when it's replaced
		class MemoryScriptAssets final : public ScriptAssets
		{
		public:
			UUID Add(const std::string& path, std::string source)
			{
				const UUID id = UUID::Generate();
				m_Ids[id] = path;
				m_Scripts[path] = ScriptFile{.Path = path, .Source = std::move(source), .Version = 1};
				return id;
			}

			void Replace(const std::string& path, std::string source)
			{
				ScriptFile& file = m_Scripts.at(path);
				file.Source = std::move(source);
				++file.Version;
			}

			std::optional<std::string> FindScript(UUID id) const override
			{
				const auto found = m_Ids.find(id);
				return found != m_Ids.end() ? std::optional(found->second) : std::nullopt;
			}

			std::expected<ScriptFile, Error> ReadScript(std::string_view path) const override
			{
				const auto found = m_Scripts.find(std::string(path));
				if (found == m_Scripts.end())
					return std::unexpected(Error(ErrorCode::NotFound, "No such script"));
				return found->second;
			}

			std::optional<uint64_t> GetVersion(std::string_view path) const override
			{
				const auto found = m_Scripts.find(std::string(path));
				return found != m_Scripts.end() ? std::optional(found->second.Version) : std::nullopt;
			}

			std::expected<ScriptPrefab, Error> LoadPrefab(std::string_view /*pathOrId*/) const override
			{
				return std::unexpected(Error(ErrorCode::NotFound, "No prefabs"));
			}

		private:
			std::map<UUID, std::string> m_Ids;
			std::map<std::string, ScriptFile> m_Scripts;
		};

		// A scene whose scripts run in a simulation
		class ScriptTest
		{
		public:
			explicit ScriptTest(ScriptSystemConfig config = {})
				: m_Scripts(m_Scene, WithAssets(std::move(config), m_Assets)), m_Simulation(m_Scene)
			{
				m_Simulation.AddSystem("Scripts", [this](SimulationContext& context) { m_Scripts.Update(context); });
			}

			Scene& GetScene() { return m_Scene; }
			ScriptSystem& GetScripts() { return m_Scripts; }
			MemoryScriptAssets& GetAssets() { return m_Assets; }

			Entity AddScripted(std::string_view name, UUID script, std::string properties = "{}")
			{
				Entity entity = m_Scene.CreateEntity(name);
				entity.Add<ScriptComponent>(ScriptComponent{.Script = script, .Properties = std::move(properties)});
				return entity;
			}

			void Run(int ticks)
			{
				for (int tick = 0; tick < ticks; ++tick)
					m_Simulation.Step({});
			}

		private:
			static ScriptSystemConfig WithAssets(ScriptSystemConfig config, const MemoryScriptAssets& assets)
			{
				config.Assets = &assets;
				return config;
			}

		private:
			Scene m_Scene;
			MemoryScriptAssets m_Assets;
			ScriptSystem m_Scripts;
			Simulation m_Simulation;
		};

		// The index of the first captured message containing the text, or npos. Scripts log with Log.Warn here:
		// Log.Info is developer logging, compiled out of Dist
		size_t FindLogged(const Testing::LogCapture& log, std::string_view text)
		{
			const std::vector<std::string> messages = log.GetMessages();
			for (size_t index = 0; index < messages.size(); ++index)
				if (messages[index].contains(text))
					return index;
			return std::string::npos;
		}

		bool Logged(const Testing::LogCapture& log, std::string_view text)
		{
			return FindLogged(log, text) != std::string::npos;
		}

		std::string Run(ScriptSystem& scripts, std::string_view source)
		{
			auto result = scripts.Run(source);
			REQUIRE_MESSAGE(result.has_value(), result.error().GetMessageText());
			return result.value_or("");
		}

		std::string RunFailing(ScriptSystem& scripts, std::string_view source)
		{
			auto result = scripts.Run(source);
			REQUIRE_FALSE(result.has_value());
			return result.has_value() ? "" : result.error().GetMessageText();
		}

	}

	TEST_CASE("Scripts run in a sandbox without files, native code or bytecode")
	{
		Scene scene;
		ScriptSystem scripts(scene);
		// Libraries that reach the system aren't there
		CHECK(Run(scripts, "return tostring(io) .. tostring(debug) .. tostring(dofile) .. tostring(loadfile)") ==
			"nilnilnilnil");
		CHECK(Run(scripts, "return tostring(package.loadlib) .. tostring(package.searchpath)") == "nilnil");
		// From os, only the time
		CHECK(Run(scripts,
				  "local names = {} for name in pairs(os) do names[#names + 1] = name end "
				  "table.sort(names) return table.concat(names, ',')") == "clock,date,time");
		CHECK(Run(scripts, "return require('os') == os") == "true");
		// load compiles text, not bytecode - even when asked to
		CHECK(Run(scripts, "return load('return 1 + 2')()") == "3");
		CHECK(
			Run(scripts,
				"local f, e = load(string.dump(function() return 1 end), 'dumped', 'b') return tostring(f) .. ' ' .. e")
				.find("attempt to load a binary chunk") != std::string::npos);
		// An explicit environment, even nil, is still passed on
		CHECK(Run(scripts, "return load('return x', 'chunk', 't', {x = 5})()") == "5");
		CHECK(
			RunFailing(scripts, "return load('return print', 'chunk', 't', nil)()()").find("nil") != std::string::npos);
		// The standard libraries scripts need are there
		CHECK(
			Run(scripts, "return string.format('%d', math.floor(2.5)) .. utf8.char(65) .. #table.pack(1, 2)") == "2A2");
		CHECK(Run(scripts, "local c = coroutine.wrap(function() coroutine.yield(4) end) return c()") == "4");
	}

	TEST_CASE("require loads the project's scripts by module name, and nothing else")
	{
		ScriptTest test;
		test.GetAssets().Add(
			"Scripts/Util.lua", "local Util = {} function Util.Double(x) return x * 2 end return Util");
		test.GetAssets().Add("Scripts/Broken.lua", "return {");
		ScriptSystem& scripts = test.GetScripts();

		CHECK(Run(scripts, "return require('Scripts.Util').Double(21)") == "42");
		// Loaded once
		CHECK(Run(scripts, "return require('Scripts.Util') == require('Scripts.Util')") == "true");
		CHECK(RunFailing(scripts, "return require('Scripts.Missing')").find("no script 'Scripts/Missing.lua'") !=
			std::string::npos);
		for (const char* name : {"../Secrets", "Scripts/Util", "Scripts..Util", ".Util", "C:\\\\Windows"})
		{
			CAPTURE(name);
			CHECK(RunFailing(scripts, fmt::format("return require('{}')", name)).find("isn't a module name") !=
				std::string::npos);
		}
		CHECK(
			RunFailing(scripts, "return require('Scripts.Broken')").find("Scripts/Broken.lua:1:") != std::string::npos);
		CHECK(RunFailing(scripts, "return require('string.dump')").find("no script") != std::string::npos);
	}

	TEST_CASE("Scripts that use too much memory fail, and the rest keep running")
	{
		Scene scene;
		ScriptSystem scripts(scene, {.MemoryLimit = size_t{16} * 1024 * 1024});
		const size_t before = scripts.GetMemoryUsed();
		CHECK(before > 0);
		CHECK(RunFailing(scripts, "local t = {} for i = 1, 1e8 do t[i] = string.rep('x', 1000) .. i end")
				  .find("not enough memory") != std::string::npos);
		CHECK(scripts.GetMemoryUsed() <= size_t{16} * 1024 * 1024);
		CHECK(Run(scripts, "collectgarbage() return 1 + 1") == "2");
		CHECK(scripts.GetMemoryUsed() < 2 * before + size_t{1024} * 1024);
	}

	TEST_CASE("Runaway scripts are stopped, even when they catch the error")
	{
		Scene scene;
		ScriptSystem scripts(scene, {.InstructionLimit = 100'000});
		CHECK(RunFailing(scripts, "while true do end").find("instructions") != std::string::npos);
		// pcall catches the first stop, but every check after it raises again
		CHECK(RunFailing(scripts, "pcall(function() while true do end end) local x = 0 for i = 1, 1e7 do x = x + 1 end")
				  .find("instructions") != std::string::npos);
		// Coroutines are stopped too
		CHECK(RunFailing(scripts, "local c = coroutine.wrap(function() while true do end end) c()")
				  .find("instructions") != std::string::npos);
		// Each run has a fresh budget
		CHECK(Run(scripts, "local x = 0 for i = 1, 1000 do x = x + i end return x") == "500500");
	}

	TEST_CASE("Scripts give the same results every run: tables iterate and random numbers come in the same order")
	{
		const std::string_view source = R"(
			local t = {}
			for _, word in ipairs({"zebra", "apple", "mango", "kiwi", "banana", "cherry", "grape", "lemon"}) do
				t[word] = true
			end
			local order = {}
			for word in pairs(t) do order[#order + 1] = word end
			return table.concat(order, ",") .. " " .. math.random(1, 1000000) .. " " .. math.random(1, 1000000)
		)";
		Scene first;
		Scene second;
		ScriptSystem one(first);
		ScriptSystem two(second);
		CHECK(Run(one, source) == Run(two, source));
	}

	TEST_CASE("Script instances are made for Script components, updated every tick, and ended when they go")
	{
		ScriptTest test;
		const UUID counter = test.GetAssets().Add("Scripts/Counter.lua", R"(
			local Counter = {Properties = {Step = 1}}
			function Counter:OnCreate() self.Count = 0 self.Entity:SetName("Created") end
			function Counter:OnUpdate(dt)
				self.Count = self.Count + self.Step
				local t = self.Entity:Get("Transform")
				t.Position = vec3(self.Count, dt, 0)
				Log.Warn("order " .. self.Entity:GetName() .. " " .. self.Count)
			end
			function Counter:OnDestroy() Log.Warn("destroyed " .. self.Count) end
			return Counter
		)");
		Entity first = test.AddScripted("First", counter);
		Entity second = test.AddScripted("Second", counter, R"({"Step": 10})");

		const Testing::LogCapture log;
		test.Run(3);
		CHECK(test.GetScripts().GetInstanceCount() == 2);
		CHECK(first.GetName() == "Created");
		CHECK(first.Get<TransformComponent>().Position == glm::vec3(3.0f, 1.0f / 60.0f, 0.0f));
		CHECK(second.Get<TransformComponent>().Position.x == 30.0f);
		CHECK(Logged(log, "order Created 3"));
		CHECK(Logged(log, "order Created 30"));

		// Removing the component ends its instance
		second.Remove<ScriptComponent>();
		test.Run(1);
		CHECK(Logged(log, "destroyed 30"));
		CHECK(test.GetScripts().GetInstanceCount() == 1);
		// And so does destroying the entity: OnDestroy is still called, though the entity is gone
		test.GetScene().DestroyEntity(first);
		test.Run(1);
		CHECK(Logged(log, "destroyed 4"));
		CHECK(test.GetScripts().GetInstanceCount() == 0);
		CHECK(test.GetScripts().GetErrorCount() == 0);
	}

	TEST_CASE("Script properties override the script's defaults, and bad ones are reported and ignored")
	{
		ScriptTest test;
		const UUID script = test.GetAssets().Add("Scripts/Props.lua", R"(
			local Props = {Properties = {Speed = 1, Name = "x", Enabled = false, Offset = vec3(1, 2, 3), Size = vec2(5)}}
			function Props:OnCreate()
				self.Offset.x = self.Offset.x + 100
				local t = self.Entity:Get("Transform")
				t.Position = vec3(self.Speed, self.Offset.x, self.Size.y)
				self.Entity:SetName(self.Name .. tostring(self.Enabled) .. " " .. math.type(self.Speed))
			end
			return Props
		)");
		Entity overridden =
			test.AddScripted("A", script, R"({"Speed": 6, "Name": "fast", "Enabled": true, "Size": [7, 8]})");
		Entity plain = test.AddScripted("B", script);
		Entity bad = test.AddScripted("C", script, R"({"Speed": "six", "Missing": 1, "Offset": [1, 2]})");
		const Entity broken = test.AddScripted("D", script, "not json");
		const Entity fractional = test.AddScripted("E", script, R"({"Speed": 2.5})");

		const Testing::LogCapture log;
		test.Run(1);
		CHECK(overridden.Get<TransformComponent>().Position == glm::vec3(6.0f, 101.0f, 8.0f));
		// Whole numbers stay integers, as they would in Lua
		CHECK(overridden.GetName() == "fasttrue integer");
		CHECK(fractional.GetName() == "xfalse float");
		// Each instance has its own copy of a default vector: the first one's change didn't reach the second
		CHECK(plain.Get<TransformComponent>().Position == glm::vec3(1.0f, 101.0f, 5.0f));
		CHECK(plain.GetName() == "xfalse integer");
		CHECK(bad.Get<TransformComponent>().Position == glm::vec3(1.0f, 101.0f, 5.0f));
		CHECK(Logged(log, "Script property 'Speed' of 'C'"));
		CHECK(Logged(log, "has no property 'Missing'"));
		CHECK(Logged(log, "Script property 'Offset' of 'C'"));
		CHECK(Logged(log, "script properties of 'D' aren't a JSON object"));
		CHECK(broken.GetName() == "xfalse integer");
		CHECK(test.GetScripts().GetErrorCount() == 4);
	}

	TEST_CASE("A script error stops only the instance that raised it, and says where")
	{
		ScriptTest test;
		const UUID failing = test.GetAssets().Add("Scripts/Failing.lua", R"(
			local Failing = {}
			function Failing:OnUpdate()
				self.Ticks = (self.Ticks or 0) + 1
				local missing = nil
				return missing.Field
			end
			return Failing
		)");
		const UUID working = test.GetAssets().Add("Scripts/Working.lua", R"(
			local Working = {}
			function Working:OnUpdate() self.Ticks = (self.Ticks or 0) + 1 self.Entity:SetName(tostring(self.Ticks)) end
			return Working
		)");
		const UUID syntax = test.GetAssets().Add("Scripts/Syntax.lua", "local Syntax = {\nfunction end\nreturn Syntax");
		test.AddScripted("Failing", failing);
		const Entity worker = test.AddScripted("Worker", working);
		test.AddScripted("Syntax", syntax);
		test.AddScripted("Unknown", UUID::Generate());

		const Testing::LogCapture log;
		test.Run(5);
		CHECK(worker.GetName() == "5");
		CHECK(Logged(log, "Scripts/Failing.lua:6: attempt to index a nil value"));
		CHECK(Logged(log, "stack traceback"));
		CHECK(Logged(log, "Scripts/Syntax.lua:2:"));
		CHECK(Logged(log, "isn't in the project"));
		// Reported once each: the failing instance got no more callbacks, and the scripts that don't load aren't tried
		// again until they change
		CHECK(test.GetScripts().GetErrorCount() == 3);
		CHECK(test.GetScripts().GetInstanceCount() == 2);
	}

	TEST_CASE("Changed scripts load again, keeping their instances' fields")
	{
		ScriptTest test;
		const UUID script = test.GetAssets().Add("Scripts/Hot.lua", R"(
			local Hot = {}
			function Hot:OnUpdate() self.Count = (self.Count or 0) + 1 self.Entity:SetName("v1 " .. self.Count) end
			return Hot
		)");
		test.GetAssets().Add("Scripts/Lib.lua", "return {Suffix = 'a'}");
		const UUID user = test.GetAssets().Add("Scripts/User.lua", R"(
			local User = {}
			function User:OnUpdate() self.Entity:SetName(require("Scripts.Lib").Suffix) end
			return User
		)");
		const Entity hot = test.AddScripted("Hot", script);
		const Entity libraryUser = test.AddScripted("User", user);
		test.Run(2);
		CHECK(hot.GetName() == "v1 2");
		CHECK(libraryUser.GetName() == "a");
		CHECK_FALSE(test.GetScripts().ReloadChanged());

		test.GetAssets().Replace("Scripts/Hot.lua", R"(
			local Hot = {}
			function Hot:OnReload() self.Reloaded = true end
			function Hot:OnUpdate()
				self.Count = self.Count + 1
				self.Entity:SetName("v2 " .. self.Count .. tostring(self.Reloaded))
			end
			return Hot
		)");
		test.GetAssets().Replace("Scripts/Lib.lua", "return {Suffix = 'b'}");
		CHECK(test.GetScripts().ReloadChanged());
		test.Run(1);
		CHECK(hot.GetName() == "v2 3true");
		CHECK(libraryUser.GetName() == "b");

		// A version that doesn't load leaves the last one running
		const Testing::LogCapture log;
		test.GetAssets().Replace("Scripts/Hot.lua", "local Hot = {");
		CHECK(test.GetScripts().ReloadChanged());
		test.Run(1);
		CHECK(hot.GetName() == "v2 4true");
		CHECK(Logged(log, "Scripts/Hot.lua doesn't compile"));
		CHECK_FALSE(test.GetScripts().ReloadChanged());
	}

	TEST_CASE("Scripts load again once a module they need is added or fixed, and not before")
	{
		ScriptTest test;
		const UUID user = test.GetAssets().Add("Scripts/User.lua", R"(
			local Names = require("Scripts.Names")
			local User = {}
			function User:OnUpdate()
				self.Entity:SetName(Names.Found)
			end
			return User
		)");
		const Entity entity = test.AddScripted("Waiting", user);
		{
			const Testing::LogCapture log;
			test.Run(1);
			CHECK(Logged(log, "no script 'Scripts/Names.lua'"));
		}
		// Still missing: nothing to load again
		CHECK_FALSE(test.GetScripts().ReloadChanged());

		test.GetAssets().Add("Scripts/Names.lua", "return {Found = 'Found'");
		{
			const Testing::LogCapture log;
			CHECK(test.GetScripts().ReloadChanged());
			CHECK(Logged(log, "Scripts/Names.lua"));
		}
		// Still broken: not loaded again until it changes
		CHECK_FALSE(test.GetScripts().ReloadChanged());

		test.GetAssets().Replace("Scripts/Names.lua", "return {Found = 'Found'}");
		CHECK(test.GetScripts().ReloadChanged());
		test.Run(1);
		CHECK(entity.GetName() == "Found");
	}

	TEST_CASE("An instance stopped by an error runs again after its script reloads")
	{
		ScriptTest test;
		const UUID script = test.GetAssets().Add("Scripts/Fixed.lua", R"(
			local Fixed = {}
			function Fixed:OnUpdate() error("not yet") end
			return Fixed
		)");
		const Entity entity = test.AddScripted("Fixed", script);
		test.Run(2);
		CHECK(test.GetScripts().GetErrorCount() == 1);
		test.GetAssets().Replace("Scripts/Fixed.lua", R"(
			local Fixed = {}
			function Fixed:OnUpdate() self.Entity:SetName("fixed") end
			return Fixed
		)");
		CHECK(test.GetScripts().ReloadChanged());
		test.Run(1);
		CHECK(entity.GetName() == "fixed");
		CHECK(test.GetScripts().GetErrorCount() == 1);
	}

	TEST_CASE("Shutting the scripts down ends every instance")
	{
		ScriptTest test;
		const UUID script = test.GetAssets().Add("Scripts/Ending.lua", R"(
			local Ending = {}
			function Ending:OnDestroy() Log.Warn("ended " .. self.Entity:GetName()) end
			return Ending
		)");
		test.AddScripted("One", script);
		test.AddScripted("Two", script);
		test.Run(1);
		const Testing::LogCapture log;
		test.GetScripts().Shutdown();
		CHECK(Logged(log, "ended One"));
		CHECK(Logged(log, "ended Two"));
		CHECK(test.GetScripts().GetInstanceCount() == 0);
	}

	TEST_CASE("Script syntax is checked without running the script")
	{
		CHECK(ScriptSystem::CheckSyntax("local x = 1 return x", "Scripts/Fine.lua").has_value());
		const auto broken = ScriptSystem::CheckSyntax("local x = 1\nreturn x +", "Scripts/Broken.lua");
		REQUIRE_FALSE(broken.has_value());
		CHECK(broken.error().GetMessageText().find("Scripts/Broken.lua:2:") != std::string::npos);
		CHECK(ScriptSystem::CheckSyntax("error('never runs')", "Scripts/Runs.lua").has_value());
	}

}
