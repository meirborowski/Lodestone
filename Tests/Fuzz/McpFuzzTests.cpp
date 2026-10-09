#include "Common/Fuzzer.h"
#include "Common/LogLevelScope.h"
#include "Common/TemporaryDirectory.h"
#include "Lodestone/Editor/EditorContext.h"
#include "Lodestone/Editor/Mcp/EditorTools.h"
#include "Lodestone/Editor/Mcp/McpServer.h"
#include "Lodestone/Scene/Entity.h"

#include <doctest/doctest.h>
#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

// Fuzzes MCP messages, which arrive from clients the editor doesn't control: the JSON-RPC layer and every tool's
// argument handling. However malformed a message, the editor answers with a valid response - never crashes, hangs or
// trips a sanitizer - and its document stays consistent

namespace Lodestone {

	namespace {

		// Tools whose arguments name places outside the project, where mutated paths could write anything
		constexpr std::array<std::string_view, 4> UnfuzzedTools = {
			"project_create", "project_open", "asset_import", "editor_quit"};

		bool CallsUnfuzzedTool(std::string_view input)
		{
			const auto message = Json::Parse(input);
			if (!message || !message->is_object() || !message->contains("params"))
				return false;
			const Json::Value& params = (*message)["params"];
			if (!params.is_object() || !params.contains("name") || !params["name"].is_string())
				return false;
			const auto name = params["name"].get<std::string>();
			return std::ranges::find(UnfuzzedTools, name) != UnfuzzedTools.end();
		}

		std::vector<std::string> MakeDictionary()
		{
			std::vector<std::string> dictionary = {"true", "false", "null", "{}", "[]", "\"\"", "1e999", "-1", "0.5",
				"4294967296", "\"11111111-1111-4111-8111-111111111111\"", "\"22222222-2222-4222-8222-222222222222\"",
				"\"00000000-0000-0000-0000-000000000000\"", "[[[[[[[[", "\\u0000", "\"../\"", "\"..\""};
			for (const char* word : {"jsonrpc", "2.0", "id", "method", "params", "initialize", "ping", "tools/list",
					 "tools/call", "name", "arguments", "entity", "parent", "index", "components", "component",
					 "fields", "path", "discardChanges", "paused", "ticks", "hold", "tap", "mouseHold", "mouseTap",
					 "width", "height", "position", "target", "after", "level", "max", "type", "contains", "Transform",
					 "Name", "Hierarchy", "PrefabInstance", "Position", "Rotation", "Scale", "entity_create",
					 "entity_delete", "entity_duplicate", "entity_set_parent", "component_set", "component_add",
					 "component_remove", "prefab_create", "prefab_instantiate", "prefab_open", "scene_open",
					 "scene_new", "document_save", "edit_undo", "edit_redo", "play_start", "play_step", "play_stop",
					 "input_send", "Scenes/A.lscene", "Prefabs/A.lprefab"})
				dictionary.push_back(fmt::format("\"{}\"", word));
			return dictionary;
		}

	}

	TEST_CASE("Fuzz the MCP server with the editor's tools")
	{
		// Tools log what agents get wrong; thousands of mutations would flood the output
		const Testing::LogLevelScope quiet(LogLevel::Off);
		const Testing::TemporaryDirectory directory("McpFuzz");
		EditorContext context;
		REQUIRE(context.CreateProject(directory.GetPath() / "Game", "Fuzz").has_value());
		// Entities with the UUIDs the corpus uses
		Scene& scene = context.GetScene();
		const auto parent = scene.CreateEntityWithId(UUID(0x11111111'1111'4111ull, 0x8111'111111111111ull), "Parent");
		const auto child = scene.CreateEntityWithId(UUID(0x22222222'2222'4222ull, 0x8222'222222222222ull), "Child");
		REQUIRE((parent.has_value() && child.has_value()));
		REQUIRE(scene.SetParent(*child, *parent).has_value());

		McpServer server("Lodestone Editor", "0.0.0", "");
		// No renderer and no way to quit: screenshots fail cleanly
		RegisterEditorTools(server, context, EditorToolHost());
		McpSession session;
		session.Initialized = true;

		Testing::FuzzOptions options = Testing::MakeFuzzOptions("Mcp");
		options.Dictionary = MakeDictionary();
		const auto report = Testing::RunFuzzer(
			[&](std::string_view input)
			{
				if (CallsUnfuzzedTool(input))
					return;
				const auto response = server.HandleText(input, session);
				if (response)
				{
					const auto json = Json::Parse(*response);
					REQUIRE(json.has_value());
					CHECK((*json)["jsonrpc"] == "2.0");
					CHECK(json->contains("result") != json->contains("error"));
				}
				// The hierarchy stays consistent: every root exists and has no parent
				const Scene& current = context.GetScene();
				for (const UUID root : current.GetRootEntities())
				{
					REQUIRE(current.Contains(root));
					CHECK(current.GetRegistry().get<HierarchyComponent>(current.FindHandle(root)).Parent.IsNil());
				}
			},
			options);
		CHECK(report.Runs > 0);
	}

}
