#include "Lodestone/Scripting/LuaAnnotations.h"

#include "Common/DescribeError.h"
#include "Common/TemporaryDirectory.h"
#include "Lodestone/Core/Environment.h"
#include "Lodestone/Core/FileSystem.h"
#include "Lodestone/Reflection/ComponentRegistry.h"
#include "Lodestone/Serialization/Json.h"

#include <doctest/doctest.h>
#include <spdlog/fmt/fmt.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace Lodestone {

	namespace {

		struct GadgetComponent
		{
			bool Enabled = true;
			uint32_t Count = 0;
			float Speed = 1.0f;
			glm::vec3 Offset{0.0f};
			glm::quat Rotation = glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f);
			std::string Label;
			UUID Target;
		};

		struct BookkeepingComponent
		{
			float Value = 0.0f;
		};

		void RegisterTestComponents(ComponentRegistry& registry)
		{
			registry.Register<GadgetComponent>("Gadget", "A test component,\nover two lines")
				.Field("Enabled", &GadgetComponent::Enabled)
				.Field("Count", &GadgetComponent::Count, {.Description = "How many"})
				.Field("Speed", &GadgetComponent::Speed, {.Description = "In meters per second"})
				.Field("Offset", &GadgetComponent::Offset)
				.Field("Rotation", &GadgetComponent::Rotation)
				.Field("Label", &GadgetComponent::Label, {.Description = "Its name", .Flags = FieldFlags::ReadOnly})
				.Field("Target", &GadgetComponent::Target);
			registry.Register<BookkeepingComponent>("Bookkeeping", "Internal", ComponentFlags::Internal)
				.Field("Value", &BookkeepingComponent::Value);
		}

		bool Contains(const std::string& text, std::string_view part)
		{
			return text.contains(part);
		}

		// The projects whose lua-language-server files are committed, so the script type-check can run on them: the
		// test scripts' and the samples
		std::vector<std::filesystem::path> GetCheckedProjects()
		{
			return {std::filesystem::path(LS_TEST_FIXTURE_DIR) / "Scripting",
				std::filesystem::path(LS_SAMPLES_DIR) / "Pong"};
		}

	}

	TEST_CASE("Annotations declare the API, and a class for every component that isn't internal")
	{
		ComponentRegistry registry;
		RegisterTestComponents(registry);
		const std::string annotations = GenerateLuaAnnotations(registry);

		// The hand-written API comes first
		CHECK(annotations.starts_with("---@meta Lodestone"));
		CHECK(Contains(annotations, "function Scene.Find(name) end"));

		CHECK(Contains(annotations,
			"---A test component, over two lines\n---@class GadgetComponent : Component\n"
			"---@field Enabled boolean \n"
			"---@field Count integer How many\n"
			"---@field Speed number In meters per second\n"
			"---@field Offset vec3 \n"
			"---@field Rotation quat \n"
			"---@field Label string Its name (read-only)\n"
			"---@field Target string? \n"));
		CHECK(Contains(annotations, "---@overload fun(self: Entity, component: \"Gadget\"): GadgetComponent?\n"));
		CHECK(Contains(annotations, "---@alias ComponentName\n---| \"Gadget\"\n\n"));
		CHECK_FALSE(Contains(annotations, "Bookkeeping"));

		CHECK(Contains(annotations, "---@alias KeyName\n"));
		CHECK(Contains(annotations, "---| \"Space\"\n"));
		CHECK(Contains(annotations, "---@alias MouseButtonName\n"));
		CHECK(Contains(annotations, "---| \"Left\"\n"));
		CHECK(Contains(annotations, "---@alias GamepadButtonName\n"));
		CHECK(Contains(annotations, "---@alias GamepadAxisName\n"));
		CHECK(Contains(annotations, "---| \"LeftX\"\n"));
	}

	TEST_CASE("Annotations cover the engine's components")
	{
		const std::string annotations = GenerateLuaAnnotations();
		for (const std::string_view component : {"Transform", "RigidBody", "Animator", "Script"})
		{
			CAPTURE(component);
			CHECK(Contains(annotations, fmt::format("---@class {}Component : Component\n", component)));
		}
	}

	TEST_CASE("The lua-language-server configuration checks Lua 5.4 against the annotations")
	{
		const auto config = Json::Parse(GenerateLuaLanguageServerConfig());
		REQUIRE_MESSAGE(config.has_value(), Testing::DescribeError(config));
		CHECK((*config)["runtime.version"] == "Lua 5.4");
		CHECK((*config)["workspace.library"] == Json::Value::array({std::string(LuaAnnotationsDirectory)}));
		CHECK((*config)["runtime.builtin"]["io"] == "disable");
		CHECK((*config)["runtime.builtin"]["debug"] == "disable");
	}

	TEST_CASE("A project gets annotations kept up to date, and a configuration it can change")
	{
		const Testing::TemporaryDirectory directory("LuaAnnotations");
		const std::filesystem::path& project = directory.GetPath();
		const std::filesystem::path annotations = project / LuaAnnotationsDirectory / LuaAnnotationsFileName;
		const std::filesystem::path config = project / LuaLanguageServerConfigFileName;

		const auto written = WriteLuaLanguageServerFiles(project);
		REQUIRE_MESSAGE(written.has_value(), Testing::DescribeError(written));
		CHECK(ReadFile(annotations) == GenerateLuaAnnotations());
		CHECK(ReadFile(config) == GenerateLuaLanguageServerConfig());

		SUBCASE("Unchanged annotations aren't rewritten")
		{
			const auto old = std::filesystem::last_write_time(annotations) - std::chrono::hours(1);
			std::filesystem::last_write_time(annotations, old);
			REQUIRE(WriteLuaLanguageServerFiles(project).has_value());
			CHECK(std::filesystem::last_write_time(annotations) == old);
		}

		SUBCASE("Annotations from another version of the engine are replaced")
		{
			REQUIRE(WriteFileAtomically(annotations, "---@meta Lodestone\n").has_value());
			REQUIRE(WriteLuaLanguageServerFiles(project).has_value());
			CHECK(ReadFile(annotations) == GenerateLuaAnnotations());
		}

		SUBCASE("The project's own configuration is kept")
		{
			REQUIRE(WriteFileAtomically(config, "{}\n").has_value());
			REQUIRE(WriteLuaLanguageServerFiles(project).has_value());
			CHECK(ReadFile(config) == "{}\n");
		}
	}

	TEST_CASE("The test scripts' and samples' committed lua-language-server files are the engine's")
	{
		// LS_UPDATE_LUA_ANNOTATIONS=1 brings them up to date, after the API changes
		const auto update = ReadEnvironmentVariable("LS_UPDATE_LUA_ANNOTATIONS");
		for (const std::filesystem::path& project : GetCheckedProjects())
		{
			CAPTURE(project.string());
			const std::filesystem::path annotations = project / LuaAnnotationsDirectory / LuaAnnotationsFileName;
			const std::filesystem::path config = project / LuaLanguageServerConfigFileName;
			if (update && *update == "1")
			{
				std::filesystem::create_directories(annotations.parent_path());
				REQUIRE(WriteFileAtomically(annotations, GenerateLuaAnnotations()).has_value());
				REQUIRE(WriteFileAtomically(config, GenerateLuaLanguageServerConfig()).has_value());
				MESSAGE("Updated " << annotations.string() << " and " << config.string());
			}
			CHECK_MESSAGE(ReadFile(annotations) == GenerateLuaAnnotations(),
				"The annotations changed: run this test with LS_UPDATE_LUA_ANNOTATIONS=1 to update the committed copy");
			CHECK_MESSAGE(ReadFile(config) == GenerateLuaLanguageServerConfig(),
				"The configuration changed: run this test with LS_UPDATE_LUA_ANNOTATIONS=1 to update the committed "
				"copy");
		}
	}

}
