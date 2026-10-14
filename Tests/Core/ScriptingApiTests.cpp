// The scripting API, exercised from Lua: each test script in Tests/Fixtures/Scripting covers an area of the API with
// checks of its own, and renames its entity "Passed" once they all hold. Together they call every function the API's
// annotations declare (see docs/Decisions/0022-scripting.md)

#include "Common/DescribeError.h"
#include "Common/LogCapture.h"
#include "Common/TemporaryDirectory.h"
#include "Lodestone/Animation/Animation.h"
#include "Lodestone/Asset/AssetRegistry.h"
#include "Lodestone/Asset/ModelImporter.h"
#include "Lodestone/Core/FileSystem.h"
#include "Lodestone/Physics/PhysicsComponents.h"
#include "Lodestone/Physics/PhysicsWorld.h"
#include "Lodestone/Scene/Entity.h"
#include "Lodestone/Scene/EntitySerializer.h"
#include "Lodestone/Scene/ModelEntities.h"
#include "Lodestone/Scripting/LuaAnnotations.h"
#include "Lodestone/Scripting/ScriptAssets.h"
#include "Lodestone/Scripting/ScriptComponents.h"
#include "Lodestone/Scripting/ScriptSystem.h"
#include "Lodestone/Simulation/Simulation.h"

#include <doctest/doctest.h>
#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Lodestone {

	namespace {

		constexpr UUID ArmId(0x7000000000004000ull, 0x8000000000000001ull);

		std::filesystem::path GetFixtureAssets()
		{
			return std::filesystem::path(LS_TEST_FIXTURE_DIR) / "Scripting" / "Assets";
		}

		const Model& GetArm()
		{
			static const Model Arm = []
			{
				auto imported =
					ModelImporter::Import(std::filesystem::path(LS_TEST_FIXTURE_DIR) / "Models" / "SkinnedArm.glb");
				REQUIRE_MESSAGE(imported.has_value(), Testing::DescribeError(imported));
				return std::move(*imported);
			}();
			return Arm;
		}

		bool Logged(const Testing::LogCapture& log, std::string_view text)
		{
			return std::ranges::any_of(
				log.GetMessages(), [text](const std::string& message) { return message.contains(text); });
		}

		// The test scripts' world: a copy of the fixture project's assets, a floor, the skinned arm, and physics,
		// scripts and animation running in a simulation, in the order play mode runs them
		class ApiTest
		{
		public:
			using InputSource = std::function<std::vector<InputCommand>(uint64_t tick)>;

			ApiTest()
				: m_Registry(CopyFixtures(m_Directory)),
				  m_ScriptAssets(m_Registry),
				  m_Physics(m_Scene, {.Models = GetModelLookup(), .WorkerThreads = 0}),
				  m_Scripts(m_Scene, {.Assets = &m_ScriptAssets, .Models = GetModelLookup(), .Physics = &m_Physics}),
				  m_Simulation(m_Scene)
			{
				const auto scanned = m_Registry.Scan();
				REQUIRE_MESSAGE(scanned.has_value(), Testing::DescribeError(scanned));

				Entity floor = m_Scene.CreateEntity("Floor");
				floor.GetTransform().Position.y = -0.5f;
				floor.Add<RigidBodyComponent>(RigidBodyComponent{.Type = std::to_underlying(BodyType::Static)});
				floor.Add<BoxColliderComponent>(BoxColliderComponent{.Size = glm::vec3(40.0f, 1.0f, 40.0f)});
				auto placed = EntitySerializer::InstantiateTree(
					m_Scene, BuildModelEntities(GetArm(), ArmId, "SkinnedArm"), EntitySerializer::IdPolicy::Regenerate);
				REQUIRE_MESSAGE(placed.has_value(), Testing::DescribeError(placed));
				placed->GetTransform().Position.x = -10.0f;
				// As play mode does, so the first tick's scripts can query the level
				m_Physics.Sync();

				m_Simulation.AddSystem("Scripts", [this](SimulationContext& context) { m_Scripts.Update(context); });
				m_Simulation.AddSystem("Physics", m_Physics);
				m_Simulation.AddSystem("ScriptEvents",
					[this](SimulationContext& context) { m_Scripts.DeliverEvents(context, m_Physics.GetEvents()); });
				m_Simulation.AddSystem("Animation", [](SimulationContext& context)
					{ AdvanceAnimators(context.GetScene(), context.GetDeltaTime(), GetModelLookup()); });
			}

			UUID GetScriptId(std::string_view path) const
			{
				const AssetInfo* info = m_Registry.FindByPath(PathFromUtf8(path));
				REQUIRE_MESSAGE(info != nullptr, path);
				return info != nullptr ? info->Metadata.Id : UUID();
			}

			// Runs a test script for ticks on an entity of its own, with input from the source; whether it passed
			bool Run(std::string_view script, int ticks, std::string properties = "{}", const InputSource& input = {},
				const std::function<void(uint64_t tick)>& afterTick = {})
			{
				Entity test = m_Scene.CreateEntity("Test");
				test.Add<ScriptComponent>(
					ScriptComponent{.Script = GetScriptId(script), .Properties = std::move(properties)});
				for (int tick = 0; tick < ticks; ++tick)
				{
					std::vector<InputCommand> commands =
						input ? input(m_Simulation.GetTick()) : std::vector<InputCommand>();
					m_Simulation.Step(commands);
					if (afterTick)
						afterTick(m_Simulation.GetTick() - 1);
				}
				return test.GetName() == "Passed" && m_Scripts.GetErrorCount() == 0;
			}

			ScriptSystem& GetScripts() { return m_Scripts; }
			const std::filesystem::path& GetAssetDirectory() const { return m_Registry.GetAssetDirectory(); }

		private:
			static std::filesystem::path CopyFixtures(const Testing::TemporaryDirectory& directory)
			{
				const std::filesystem::path assets = directory.GetPath() / "Assets";
				std::filesystem::copy(GetFixtureAssets(), assets, std::filesystem::copy_options::recursive);
				return assets;
			}

			static ModelLookup GetModelLookup()
			{
				return [](UUID id) -> const Model* { return id == ArmId ? &GetArm() : nullptr; };
			}

		private:
			Testing::TemporaryDirectory m_Directory{"ScriptingApi"};
			AssetRegistry m_Registry;
			ProjectScriptAssets m_ScriptAssets;
			Scene m_Scene;
			PhysicsWorld m_Physics;
			ScriptSystem m_Scripts;
			Simulation m_Simulation;
		};

	}

	TEST_CASE("Scripts do vector and rotation math")
	{
		ApiTest test;
		CHECK(test.Run("Scripts/MathTest.lua", 1));
	}

	TEST_CASE("Scripts reach entities and their components")
	{
		ApiTest test;
		CHECK(test.Run("Scripts/EntityTest.lua", 1));
	}

	TEST_CASE("Scripts create, find, instance and destroy entities")
	{
		ApiTest test;
		CHECK(test.Run("Scripts/SceneTest.lua", 1));
	}

	TEST_CASE("Scripts read their players' input")
	{
		ApiTest test;
		const auto input = [](uint64_t tick)
		{
			InputCommand first{.Tick = tick, .Player = 0};
			first.KeysDown.set(std::to_underlying(Key::W));
			if (tick == 0)
			{
				first.KeysPressed.set(std::to_underlying(Key::W));
				first.KeysDown.set(std::to_underlying(Key::Space));
				first.KeysPressed.set(std::to_underlying(Key::Space));
				first.MouseButtonsDown.set(std::to_underlying(MouseButton::Left));
				first.MouseButtonsPressed.set(std::to_underlying(MouseButton::Left));
				first.MousePosition = glm::vec2(100.0f, 200.0f);
				first.MouseDelta = glm::vec2(3.0f, 4.0f);
				first.ScrollDelta = glm::vec2(0.0f, 1.0f);
				first.GamepadConnected = true;
				first.GamepadButtonsDown.set(std::to_underlying(GamepadButton::A));
				first.GamepadButtonsPressed.set(std::to_underlying(GamepadButton::A));
				first.GamepadAxes[std::to_underlying(GamepadAxis::LeftX)] = 0.5f;
			}
			else
			{
				first.KeysReleased.set(std::to_underlying(Key::Space));
				first.MouseButtonsReleased.set(std::to_underlying(MouseButton::Left));
				first.GamepadConnected = true;
				first.GamepadButtonsReleased.set(std::to_underlying(GamepadButton::A));
			}
			InputCommand second{.Tick = tick, .Player = 1};
			second.KeysDown.set(std::to_underlying(Key::Up));
			return std::vector<InputCommand>{first, second};
		};
		CHECK(test.Run("Scripts/InputTest.lua", 2, "{}", input));
	}

	TEST_CASE("Scripts cast rays, push bodies and hear about collisions and triggers")
	{
		ApiTest test;
		const std::string properties =
			fmt::format(R"({{"BallScript": "{}"}})", test.GetScriptId("Scripts/BallRecorder.lua").ToString());
		CHECK(test.Run("Scripts/PhysicsTest.lua", 303, properties));
	}

	TEST_CASE("Scripts play, fade, pause and stop animations")
	{
		ApiTest test;
		CHECK(test.Run("Scripts/AnimationTest.lua", 51));
	}

	TEST_CASE("Scripts read the time and write to the log")
	{
		ApiTest test;
		const Testing::LogCapture log;
		CHECK(test.Run("Scripts/TimeLogTest.lua", 3));
		CHECK(Logged(log, "warning a warning from a script\tvec3(1, 2, 3)"));
		CHECK(Logged(log, "error an error from a script"));
#if !defined(LS_CONFIG_DIST)
		// Information is developer logging, which Dist leaves out
		CHECK(Logged(log, "info info from a script\t1\ttrue"));
		CHECK(Logged(log, "info print from a script"));
#endif
	}

	TEST_CASE("Scripts live, call each other, end, and reload keeping their state")
	{
		ApiTest test;
		const Testing::LogCapture log;
		const std::string properties = fmt::format(R"({{"Speed": 4, "Target": [1, 2, 3], "HelperScript": "{}"}})",
			test.GetScriptId("Scripts/Helper.lua").ToString());
		const std::filesystem::path script = test.GetAssetDirectory() / "Scripts" / "LifecycleTest.lua";
		const auto reload = [&](uint64_t tick)
		{
			if (tick != 3)
				return;
			// The same script, saved again: a later write time makes it a change
			auto source = ReadFile(script);
			REQUIRE(source.has_value());
			REQUIRE(WriteFileAtomically(script, source.value_or("") + "\n").has_value());
			std::filesystem::last_write_time(
				script, std::filesystem::last_write_time(script) + std::chrono::seconds(5));
			CHECK(test.GetScripts().ReloadChanged());
		};
		CHECK(test.Run("Scripts/LifecycleTest.lua", 5, properties, {}, reload));
		CHECK(Logged(log, "Helper ended"));
		CHECK(!Logged(log, "LifecycleTest ended"));
		test.GetScripts().Shutdown();
		CHECK(Logged(log, "LifecycleTest ended"));
	}

	TEST_CASE("The test scripts call every function and callback the API's annotations declare")
	{
		// Every script's text, together
		std::string scripts;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(GetFixtureAssets()))
			if (entry.path().extension() == ".lua")
			{
				auto text = ReadFile(entry.path());
				REQUIRE(text.has_value());
				scripts += text.value_or("");
			}

		const std::string annotations = GenerateLuaAnnotations();
		std::set<std::string> missing;
		// Functions: function Scene.Find(...) is called as Scene.Find(, function Entity:GetName(...) as :GetName(
		const std::regex function(R"(^function (\w+)([.:])(\w+)\()");
		// Callbacks: ---@field OnUpdate? fun(...)
		const std::regex callback(R"(^---@field (On\w+)\?)");
		size_t declared = 0;
		std::istringstream lines(annotations);
		for (std::string text; std::getline(lines, text);)
		{
			std::smatch match;
			std::string call;
			if (std::regex_search(text, match, function))
				call = match[2] == "." ? fmt::format("{}.{}(", match[1].str(), match[3].str())
									   : fmt::format(":{}(", match[3].str());
			else if (std::regex_search(text, match, callback))
				call = fmt::format(":{}(", match[1].str());
			else
				continue;
			++declared;
			if (!scripts.contains(call))
				missing.insert(call);
		}
		CHECK(declared > 80);
		for (const std::string& call : missing)
			FAIL_CHECK("No test script calls " << call);
	}

	TEST_CASE("Every function scripts can call is in the API's annotations")
	{
		// What the annotations declare: Type.Function for functions and methods, Type.Field for each class's fields
		std::set<std::string> annotated;
		const std::regex function(R"(^function (\w+)[.:](\w+)\()");
		const std::regex type(R"(^---@class (\w+))");
		const std::regex field(R"(^---@field (\w+))");
		std::istringstream lines(GenerateLuaAnnotations());
		std::string currentType;
		for (std::string text; std::getline(lines, text);)
		{
			std::smatch match;
			if (std::regex_search(text, match, function))
				annotated.insert(match[1].str() + "." + match[2].str());
			else if (std::regex_search(text, match, type))
				currentType = match[1].str();
			else if (std::regex_search(text, match, field))
				annotated.insert(currentType + "." + match[1].str());
		}

		// What the scripts' sandbox has: the functions of the API's tables, and of its types' metatables, which hold
		// their methods, static functions and field accessors. Lua's metamethods and sol2's own entries are left out:
		// new, which only raises an error, as the types have no constructor of that name
		ApiTest test;
		const auto bound = test.GetScripts().Run(R"lua(
			local names = {}
			local function add(name, functions)
				for key, value in next, functions do
					if type(key) == "string" and type(value) == "function" and not key:find("^__") and key ~= "new" then
						names[#names + 1] = name .. "." .. key
					end
				end
			end
			for _, name in ipairs({"Scene", "Input", "Physics", "Animation", "Log", "Time"}) do
				add(name, _G[name])
			end
			local entity = Scene.Create("Instance")
			local instances = {vec2 = vec2(), vec3 = vec3(), vec4 = vec4(), quat = quat(), Entity = entity,
				Component = entity:Get("Transform")}
			for name, instance in pairs(instances) do
				add(name, getmetatable(instance))
			end
			table.sort(names)
			return table.concat(names, "\n")
		)lua");
		REQUIRE_MESSAGE(bound.has_value(), Testing::DescribeError(bound));
		std::istringstream names(*bound);
		size_t count = 0;
		for (std::string name; std::getline(names, name); ++count)
			if (!annotated.contains(name))
				FAIL_CHECK(name << " has no annotation");
		CHECK(count > 80);
	}

}
