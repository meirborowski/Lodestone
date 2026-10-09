#include "Lodestone/Editor/Mcp/EditorTools.h"

#include "Common/DescribeError.h"
#include "Common/LogLevelScope.h"
#include "Common/TemporaryDirectory.h"
#include "Lodestone/Core/FileSystem.h"
#include "Lodestone/Scene/Entity.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Lodestone {

	namespace {

		// Every tool the editor offers. A new tool is added here deliberately, with its tests
		constexpr std::array<std::string_view, 38> ToolNames = {"project_info", "project_create", "project_open",
			"scene_new", "scene_open", "prefab_open", "document_save", "scene_hierarchy", "entity_create",
			"entity_delete", "entity_duplicate", "entity_set_parent", "entity_get", "entity_find", "selection_set",
			"component_types", "component_add", "component_remove", "component_set", "prefab_create",
			"prefab_instantiate", "edit_undo", "edit_redo", "edit_history", "play_start", "play_stop", "play_pause",
			"play_step", "play_status", "input_send", "viewport_screenshot", "camera_set", "camera_frame", "log_read",
			"asset_list", "asset_import", "asset_rescan", "editor_quit"};

		UUID ToId(const std::string& text)
		{
			const std::optional<UUID> id = UUID::Parse(text);
			REQUIRE_MESSAGE(id.has_value(), text);
			return id.value_or(UUID());
		}

		std::vector<uint8_t> DecodeBase64(std::string_view text)
		{
			constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			std::vector<uint8_t> bytes;
			uint32_t buffer = 0;
			int bits = 0;
			for (const char character : text)
			{
				if (character == '=')
					break;
				const size_t value = alphabet.find(character);
				REQUIRE(value != std::string_view::npos);
				buffer = (buffer << 6) | static_cast<uint32_t>(value);
				bits += 6;
				if (bits >= 8)
				{
					bits -= 8;
					bytes.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
				}
			}
			return bytes;
		}

		// An editor with its MCP tools, and a stand-in for the renderer that paints screenshots one color
		class Fixture
		{
		public:
			explicit Fixture(bool withProject = true)
				: m_Server("Lodestone Editor", "0.0.0", "")
			{
				EditorToolHost host;
				host.RequestQuit = [this] { ++QuitRequests; };
				host.Capture = [this](uint32_t width, uint32_t height) -> std::expected<Image, Error>
				{
					++CaptureCount;
					if (CaptureFails)
						return std::unexpected(Error(ErrorCode::DeviceError, "No GPU today"));
					Image image(width, height);
					for (uint32_t y = 0; y < height; ++y)
					{
						for (uint32_t x = 0; x < width; ++x)
							image.SetPixel(x, y, {.R = 10, .G = 20, .B = 30, .A = 255});
					}
					return image;
				};
				RegisterEditorTools(m_Server, Context, std::move(host));
				if (withProject)
					Ok("project_create", {{"directory", GetProjectDirectory()}, {"name", "Tool Tests"}});
			}

			McpToolResult Call(std::string_view name, const Json::Value& arguments = Json::Value::object())
			{
				REQUIRE_MESSAGE(m_Server.FindTool(name) != nullptr, name);
				return m_Server.CallTool(name, arguments);
			}

			// Calls a tool that must succeed, and returns its structured content (or its text, for text results)
			Json::Value Ok(std::string_view name, const Json::Value& arguments = Json::Value::object())
			{
				const McpToolResult result = Call(name, arguments);
				INFO("Tool: ", name, " ", arguments.dump());
				REQUIRE_MESSAGE(!result.IsError, result.Content[0]["text"].get<std::string>());
				if (result.StructuredContent)
					return *result.StructuredContent;
				return result.Content[0]["text"];
			}

			// Calls a tool that must fail, and returns its message
			std::string Fails(std::string_view name, const Json::Value& arguments = Json::Value::object())
			{
				const McpToolResult result = Call(name, arguments);
				INFO("Tool: ", name, " ", arguments.dump());
				REQUIRE(result.IsError);
				CHECK_FALSE(result.StructuredContent.has_value());
				return result.Content[0]["text"].get<std::string>();
			}

			std::string CreateEntity(const std::string& name, const Json::Value& parent = Json::Value(),
				const Json::Value& components = Json::Value::object())
			{
				Json::Value arguments = {{"name", name}, {"components", components}};
				if (!parent.is_null())
					arguments["parent"] = parent;
				return Ok("entity_create", arguments)["id"].get<std::string>();
			}

			Entity Get(const std::string& id) { return Context.GetScene().FindEntity(ToId(id)); }

			std::string GetProjectDirectory() const { return PathToUtf8(Directory.GetPath() / "Game"); }
			std::filesystem::path GetAssetDirectory() const { return Directory.GetPath() / "Game" / "Assets"; }
			const McpServer& GetServer() const { return m_Server; }

			Testing::TemporaryDirectory Directory{"EditorTools"};
			EditorContext Context;
			int CaptureCount = 0;
			bool CaptureFails = false;
			int QuitRequests = 0;

		private:
			McpServer m_Server;
		};

		std::vector<std::string> GetHierarchyNames(const Json::Value& hierarchy)
		{
			std::vector<std::string> names;
			for (const Json::Value& entity : hierarchy["entities"])
				names.push_back(entity["name"].get<std::string>());
			return names;
		}

	}

	TEST_CASE("The editor registers every tool, with an object schema, a title and a description")
	{
		const Fixture fixture(false);
		std::set<std::string_view> registered;
		for (const McpTool& tool : fixture.GetServer().GetTools())
		{
			CAPTURE(tool.Name);
			registered.insert(tool.Name);
			CHECK(tool.InputSchema["type"] == "object");
			CHECK(tool.InputSchema.contains("properties"));
			CHECK_FALSE(tool.Title.empty());
			CHECK_FALSE(tool.Description.empty());
			CHECK(std::ranges::all_of(tool.Name, [](char c) { return (c >= 'a' && c <= 'z') || c == '_'; }));
		}
		CHECK(registered == std::set<std::string_view>(ToolNames.begin(), ToolNames.end()));
		CHECK(fixture.GetServer().GetTools().size() == ToolNames.size());
	}

	TEST_CASE("Every tool tolerates missing and wrongly typed arguments")
	{
		const Testing::LogLevelScope quiet(LogLevel::Off);
		const std::vector<Json::Value> badArguments = {Json::Value::object(),
			{{"entity", 5}, {"path", 7}, {"name", false}, {"directory", Json::Value::array()}, {"parent", 3.5},
				{"index", -1}, {"components", "x"}, {"fields", 1}, {"component", 2}, {"ticks", "many"}, {"hold", "W"},
				{"width", 1e9}, {"height", -5}, {"position", {1}}, {"target", "here"}, {"after", -3}, {"level", "loud"},
				{"max", 0}, {"type", "Mesh"}, {"source", 5}, {"paused", "yes"}, {"discardChanges", "no"},
				{"contains", 1}, {"overwrite", "x"}},
			{{"entity", "not-a-uuid"}, {"parent", "also-not"}, {"path", "../outside.lscene"}, {"name", ""}}};
		for (const std::string_view name : ToolNames)
		{
			for (const Json::Value& arguments : badArguments)
			{
				Fixture fixture;
				CAPTURE(name);
				CAPTURE(arguments.dump());
				// Whatever the outcome, it's a result, not a crash, and the document stays consistent
				const McpToolResult result = fixture.Call(name, arguments);
				CHECK(result.Content.is_array());
				CHECK_FALSE(result.Content.empty());
				const Scene& scene = fixture.Context.GetScene();
				for (const UUID root : scene.GetRootEntities())
					CHECK(scene.Contains(root));
			}
		}
	}

	TEST_CASE("project_info describes the project, the document, the play state and the history")
	{
		Fixture empty(false);
		const Json::Value none = empty.Ok("project_info");
		CHECK(none["project"].is_null());
		CHECK(none["document"]["kind"] == "scene");
		CHECK(none["document"]["path"].is_null());
		CHECK(none["playState"] == "Editing");
		CHECK(none["selection"].is_null());

		Fixture fixture;
		const std::string id = fixture.CreateEntity("Thing");
		fixture.Ok("selection_set", {{"entity", id}});
		const Json::Value info = fixture.Ok("project_info");
		CHECK(info["project"]["name"] == "Tool Tests");
		CHECK(info["project"]["tickRate"] == 60);
		CHECK(info["project"]["directory"] == fixture.GetProjectDirectory());
		CHECK(info["document"]["unsavedChanges"] == true);
		CHECK(info["document"]["entityCount"] == 1);
		CHECK(info["selection"] == id);
		CHECK(info["canUndo"] == true);
		CHECK(info["canRedo"] == false);
	}

	TEST_CASE("project_create and project_open refuse to lose unsaved changes unless told to")
	{
		Fixture fixture;
		fixture.CreateEntity("Unsaved");
		const std::string other = PathToUtf8(fixture.Directory.GetPath() / "Other");

		CHECK(fixture.Fails("project_create", {{"directory", other}, {"name", "Other"}}).contains("unsaved"));
		CHECK(fixture.Fails("project_open", {{"directory", fixture.GetProjectDirectory()}}).contains("unsaved"));
		CHECK(fixture.Context.GetScene().GetEntityCount() == 1);

		fixture.Ok("project_create", {{"directory", other}, {"name", "Other"}, {"discardChanges", true}});
		CHECK(fixture.Context.GetProject()->GetSettings().Name == "Other");
		CHECK(fixture.Ok("project_open", {{"directory", fixture.GetProjectDirectory()}}) ==
			"Opened project 'Tool Tests'");
		CHECK(fixture.Context.GetProject()->GetSettings().Name == "Tool Tests");
	}

	TEST_CASE("project_create and project_open fail where they can't work")
	{
		Fixture fixture;

		fixture.Fails("project_create", {{"directory", fixture.GetProjectDirectory()}, {"name", "Again"}});
		fixture.Fails("project_open", {{"directory", PathToUtf8(fixture.Directory.GetPath() / "Missing")}});
		fixture.Fails("project_create", {{"name", "No directory"}});
		fixture.Fails("project_open");
	}

	TEST_CASE("scene_new replaces the document, guarding unsaved changes")
	{
		Fixture fixture;
		fixture.CreateEntity("Unsaved");

		fixture.Fails("scene_new");
		fixture.Ok("scene_new", {{"discardChanges", true}});

		CHECK(fixture.Context.GetScene().GetEntityCount() == 0);
		fixture.Ok("scene_new");
	}

	TEST_CASE("document_save, scene_open and scene_hierarchy round-trip a scene")
	{
		Fixture fixture;
		const std::string world = fixture.CreateEntity("World");
		const std::string house = fixture.CreateEntity("House", world);
		fixture.CreateEntity("Door", house);
		fixture.CreateEntity("Sky");

		const std::string unsaved = fixture.Fails("document_save");
		CHECK(unsaved.contains("path"));
		CHECK(fixture.Ok("document_save", {{"path", "Scenes/Town.lscene"}}) == "Saved Scenes/Town.lscene");
		CHECK(fixture.Ok("document_save") == "Saved Scenes/Town.lscene");
		fixture.Ok("scene_new");
		fixture.Ok("scene_open", {{"path", "Scenes/Town.lscene"}});

		const Json::Value hierarchy = fixture.Ok("scene_hierarchy");
		CHECK(GetHierarchyNames(hierarchy) == std::vector<std::string>{"World", "House", "Door", "Sky"});
		CHECK(hierarchy["entities"][0]["depth"] == 0);
		CHECK(hierarchy["entities"][1]["depth"] == 1);
		CHECK(hierarchy["entities"][2]["depth"] == 2);
		CHECK(hierarchy["entities"][2]["parent"] == house);
		CHECK(hierarchy["entities"][3]["parent"].is_null());
	}

	TEST_CASE("Documents can't be saved or opened outside the project's assets or with the wrong extension")
	{
		Fixture fixture;
		fixture.CreateEntity("A");

		fixture.Fails("document_save", {{"path", "../Escape.lscene"}});
		fixture.Fails("document_save", {{"path", "Scene.txt"}});
		fixture.Fails("document_save", {{"path", PathToUtf8(fixture.Directory.GetPath() / "Absolute.lscene")}});
		fixture.Ok("document_save", {{"path", "Main.lscene"}});
		fixture.Fails("scene_open", {{"path", "Missing.lscene"}});
		fixture.Fails("scene_open", {{"path", "../../Project.lsproject"}});
		fixture.Fails("prefab_open", {{"path", "Main.lscene"}});
	}

	TEST_CASE("prefab_create, prefab_open and prefab_instantiate make, edit and instance prefabs")
	{
		Fixture fixture;
		const std::string tree =
			fixture.CreateEntity("Tree", Json::Value(), {{"Transform", {{"Scale", {1.0, 4.0, 1.0}}}}});
		fixture.CreateEntity("Leaves", tree);

		const Json::Value created = fixture.Ok("prefab_create", {{"entity", tree}, {"path", "Prefabs/Tree.lprefab"}});
		CHECK(created["path"] == "Prefabs/Tree.lprefab");
		CHECK(created["asset"].is_string());
		fixture.Fails("prefab_create", {{"entity", tree}, {"path", "Prefabs/Tree.lscene"}});
		fixture.Fails("prefab_create", {{"entity", UUID::Generate().ToString()}, {"path", "Prefabs/Ghost.lprefab"}});

		const std::string forest = fixture.CreateEntity("Forest");
		const Json::Value instance =
			fixture.Ok("prefab_instantiate", {{"path", "Prefabs/Tree.lprefab"}, {"parent", forest}, {"index", 0}});
		const std::string instanceId = instance["id"].get<std::string>();
		CHECK(instanceId != tree);
		CHECK(instance["done"] == "Instance prefab 'Tree'");
		const Json::Value described = fixture.Ok("entity_get", {{"entity", instanceId}});
		CHECK(described["name"] == "Tree");
		CHECK(described["parent"] == forest);
		CHECK(described["children"].size() == 1);
		CHECK(described["components"]["PrefabInstance"]["Prefab"] == created["asset"]);
		CHECK(described["components"]["Transform"]["Scale"] == Json::Value{1.0, 4.0, 1.0});
		fixture.Fails("prefab_instantiate", {{"path", "Prefabs/Missing.lprefab"}});
		fixture.Fails(
			"prefab_instantiate", {{"path", "Prefabs/Tree.lprefab"}, {"parent", UUID::Generate().ToString()}});

		// Editing the prefab as a document
		fixture.Ok("document_save", {{"path", "Main.lscene"}});
		fixture.Ok("prefab_open", {{"path", "Prefabs/Tree.lprefab"}});
		const Json::Value info = fixture.Ok("project_info");
		CHECK(info["document"]["kind"] == "prefab");
		CHECK(info["document"]["path"] == "Prefabs/Tree.lprefab");
		CHECK(info["document"]["entityCount"] == 2);
	}

	TEST_CASE("entity_create places entities with components under parents, at an index")
	{
		Fixture fixture;
		const std::string parent = fixture.CreateEntity("Parent");
		fixture.CreateEntity("Second", parent);

		const Json::Value created = fixture.Ok("entity_create",
			{{"name", "First"}, {"parent", parent}, {"index", 0},
				{"components", {{"Transform", {{"Position", {1.0, 2.0, 3.0}}}}}}});
		CHECK(created["done"] == "Create entity 'First'");
		const Entity entity = fixture.Get(created["id"].get<std::string>());
		REQUIRE(entity);
		CHECK(entity.GetTransform().Position == glm::vec3(1.0f, 2.0f, 3.0f));
		CHECK(fixture.Get(parent).Get<HierarchyComponent>().Children.front() == entity.GetId());

		// A name is optional
		CHECK(fixture.Get(fixture.Ok("entity_create")["id"].get<std::string>()).GetName() == "Entity");
		fixture.Fails("entity_create", {{"parent", UUID::Generate().ToString()}});
		fixture.Fails("entity_create", {{"components", {{"Transform", {{"Position", "up"}}}}}});
		fixture.Fails("entity_create", {{"components", {{"Missing", Json::Value::object()}}}});
		fixture.Fails("entity_create", {{"index", -1}});
	}

	TEST_CASE("entity_delete removes an entity and its descendants, undoably")
	{
		Fixture fixture;
		const std::string parent = fixture.CreateEntity("Parent");
		fixture.CreateEntity("Child", parent);

		CHECK(fixture.Ok("entity_delete", {{"entity", parent}})["done"] == "Delete entity 'Parent'");
		CHECK(fixture.Context.GetScene().GetEntityCount() == 0);
		fixture.Ok("edit_undo");
		CHECK(fixture.Context.GetScene().GetEntityCount() == 2);
		fixture.Fails("entity_delete", {{"entity", UUID::Generate().ToString()}});
		fixture.Fails("entity_delete", {{"entity", "Parent"}});
	}

	TEST_CASE("entity_duplicate copies an entity's tree with new UUIDs")
	{
		Fixture fixture;
		const std::string original = fixture.CreateEntity("Original");
		fixture.CreateEntity("Child", original);

		const std::string copy = fixture.Ok("entity_duplicate", {{"entity", original}})["id"].get<std::string>();

		CHECK(copy != original);
		CHECK(fixture.Context.GetScene().GetEntityCount() == 4);
		CHECK(GetHierarchyNames(fixture.Ok("scene_hierarchy")) ==
			std::vector<std::string>{"Original", "Child", "Original", "Child"});
		fixture.Fails("entity_duplicate", {{"entity", UUID::Generate().ToString()}});
	}

	TEST_CASE("entity_set_parent moves entities, and refuses cycles")
	{
		Fixture fixture;
		const std::string a = fixture.CreateEntity("A");
		const std::string b = fixture.CreateEntity("B");

		fixture.Ok("entity_set_parent", {{"entity", b}, {"parent", a}});
		CHECK(fixture.Get(b).Get<HierarchyComponent>().Parent == ToId(a));
		fixture.Fails("entity_set_parent", {{"entity", a}, {"parent", b}});
		fixture.Ok("entity_set_parent", {{"entity", b}, {"parent", nullptr}, {"index", 0}});
		CHECK(GetHierarchyNames(fixture.Ok("scene_hierarchy")) == std::vector<std::string>{"B", "A"});
		fixture.Fails("entity_set_parent", {{"entity", a}, {"parent", UUID::Generate().ToString()}});
	}

	TEST_CASE("entity_get describes an entity with every component except internal ones")
	{
		Fixture fixture;
		const std::string parent =
			fixture.CreateEntity("Parent", Json::Value(), {{"Transform", {{"Position", {10.0, 0.0, 0.0}}}}});
		const std::string child =
			fixture.CreateEntity("Child", parent, {{"Transform", {{"Position", {0.0, 1.0, 0.0}}}}});
		fixture.Ok("play_start");
		fixture.Ok("play_stop");

		const Json::Value described = fixture.Ok("entity_get", {{"entity", child}});

		CHECK(described["id"] == child);
		CHECK(described["name"] == "Child");
		CHECK(described["parent"] == parent);
		CHECK(described["children"].empty());
		CHECK(described["worldPosition"] == Json::Value{10.0, 1.0, 0.0});
		CHECK(described["components"]["Transform"]["Position"] == Json::Value{0.0, 1.0, 0.0});
		CHECK(described["components"]["ID"]["ID"] == child);
		CHECK_FALSE(described["components"].contains("PreviousTransform"));
		fixture.Fails("entity_get", {{"entity", UUID::Generate().ToString()}});
	}

	TEST_CASE("entity_find finds entities by exact name, or by part of it ignoring case")
	{
		Fixture fixture;
		fixture.CreateEntity("Enemy Tank");
		fixture.CreateEntity("enemy scout");
		fixture.CreateEntity("Player");

		CHECK(fixture.Ok("entity_find", {{"name", "Player"}})["entities"].size() == 1);
		CHECK(fixture.Ok("entity_find", {{"name", "player"}})["entities"].empty());
		const Json::Value enemies = fixture.Ok("entity_find", {{"name", "ENEMY"}, {"contains", true}})["entities"];
		REQUIRE(enemies.size() == 2);
		CHECK(enemies[0]["name"] == "Enemy Tank");
		CHECK(enemies[1]["name"] == "enemy scout");
		CHECK(enemies[0]["id"].is_string());
	}

	TEST_CASE("selection_set selects and clears, but only entities in the scene")
	{
		Fixture fixture;
		const std::string id = fixture.CreateEntity("Pick me");

		fixture.Ok("selection_set", {{"entity", id}});
		CHECK(fixture.Context.GetSelection() == ToId(id));
		fixture.Fails("selection_set", {{"entity", UUID::Generate().ToString()}});
		CHECK(fixture.Context.GetSelection() == ToId(id));
		fixture.Ok("selection_set", {{"entity", nullptr}});
		CHECK(fixture.Context.GetSelection().IsNil());
	}

	TEST_CASE("component_types describes every editable component and its fields")
	{
		Fixture fixture(false);

		const Json::Value components = fixture.Ok("component_types")["components"];

		const auto find = [&components](std::string_view name) -> const Json::Value*
		{
			for (const Json::Value& component : components)
			{
				if (component["name"] == name)
					return &component;
			}
			return nullptr;
		};
		const Json::Value* transform = find("Transform");
		REQUIRE(transform != nullptr);
		CHECK((*transform)["required"] == true);
		REQUIRE((*transform)["fields"].size() == 3);
		const Json::Value& position = (*transform)["fields"][0];
		CHECK(position["name"] == "Position");
		CHECK(position["type"] == "Vec3");
		CHECK(position["default"] == Json::Value{0.0, 0.0, 0.0});
		CHECK(position["readOnly"] == false);
		CHECK(position["replicated"] == true);
		CHECK_FALSE(position["description"].get<std::string>().empty());
		CHECK((*find("Hierarchy"))["fields"][0]["readOnly"] == true);
		CHECK((*find("PrefabInstance"))["required"] == false);
		CHECK(find("PreviousTransform") == nullptr);
	}

	TEST_CASE("component_add, component_set and component_remove edit components, undoably")
	{
		Fixture fixture;
		const std::string id = fixture.CreateEntity("Thing");

		fixture.Ok("component_add", {{"entity", id}, {"component", "PrefabInstance"}});
		CHECK(fixture.Get(id).Has<PrefabInstanceComponent>());
		fixture.Fails("component_add", {{"entity", id}, {"component", "PrefabInstance"}});
		fixture.Fails("component_add", {{"entity", id}, {"component", "Nonexistent"}});
		fixture.Fails("component_add", {{"entity", id}, {"component", "PreviousTransform"}});

		const Json::Value set = fixture.Ok("component_set",
			{{"entity", id}, {"component", "Transform"},
				{"fields",
					{{"Position", {1.0, 2.0, 3.0}}, {"Rotation", {0.0, 0.0, 0.0, 1.0}}, {"Scale", {2.0, 2.0, 2.0}}}}});
		CHECK(set["done"] == "Set Transform Position, Rotation, Scale");
		CHECK(fixture.Get(id).GetTransform().Scale == glm::vec3(2.0f));
		fixture.Ok("component_set", {{"entity", id}, {"component", "Name"}, {"fields", {{"Name", "Renamed"}}}});
		CHECK(fixture.Get(id).GetName() == "Renamed");
		fixture.Fails("component_set", {{"entity", id}, {"component", "Transform"}, {"fields", {{"Position", {1.0}}}}});
		fixture.Fails("component_set", {{"entity", id}, {"component", "ID"}, {"fields", {{"ID", id}}}});
		fixture.Fails("component_set", {{"entity", id}, {"component", "Transform"}});

		fixture.Ok("component_remove", {{"entity", id}, {"component", "PrefabInstance"}});
		CHECK_FALSE(fixture.Get(id).Has<PrefabInstanceComponent>());
		fixture.Fails("component_remove", {{"entity", id}, {"component", "Transform"}});
		fixture.Fails("component_remove", {{"entity", id}, {"component", "PrefabInstance"}});

		fixture.Ok("edit_undo");
		CHECK(fixture.Get(id).Has<PrefabInstanceComponent>());
	}

	TEST_CASE("edit_undo, edit_redo and edit_history work through the shared history")
	{
		Fixture fixture;
		fixture.Fails("edit_undo");
		fixture.Fails("edit_redo");
		const std::string id = fixture.CreateEntity("First");
		fixture.Ok("component_set", {{"entity", id}, {"component", "Name"}, {"fields", {{"Name", "Second"}}}});

		const Json::Value history = fixture.Ok("edit_history");
		CHECK(history["undo"] == Json::Value{"Set Name Name", "Create entity 'First'"});
		CHECK(history["redo"].is_null());

		CHECK(fixture.Ok("edit_undo")["undone"] == "Set Name Name");
		CHECK(fixture.Get(id).GetName() == "First");
		CHECK(fixture.Ok("edit_history")["redo"] == "Set Name Name");
		CHECK(fixture.Ok("edit_redo")["redone"] == "Set Name Name");
		CHECK(fixture.Get(id).GetName() == "Second");
	}

	TEST_CASE("Play mode tools start, pause, step and stop, restoring the scene")
	{
		Fixture fixture;
		const std::string id = fixture.CreateEntity("Mover");

		CHECK(fixture.Ok("play_status") == Json::Value{{"state", "Editing"}, {"tick", 0}, {"queuedInput", 0}});
		fixture.Fails("play_stop");
		fixture.Fails("play_step");
		fixture.Fails("play_pause");
		CHECK(fixture.Ok("play_start")["state"] == "Playing");
		fixture.Fails("play_start");
		// Edits are refused while playing
		fixture.Fails("entity_delete", {{"entity", id}});
		fixture.Fails("edit_undo");
		fixture.Fails("document_save", {{"path", "Main.lscene"}});

		CHECK(fixture.Ok("play_pause")["state"] == "Paused");
		const Json::Value stepped = fixture.Ok("play_step", {{"ticks", 5}});
		CHECK(stepped["tick"] == 5);
		CHECK(fixture.Ok("play_step")["tick"] == 6);
		fixture.Fails("play_step", {{"ticks", 0}});
		fixture.Fails("play_step", {{"ticks", 10001}});
		CHECK(fixture.Ok("play_pause", {{"paused", false}})["state"] == "Playing");

		// Gameplay changes the scene; stopping undoes it
		fixture.Get(id).GetTransform().Position = glm::vec3(9.0f);
		CHECK(fixture.Ok("play_stop")["state"] == "Editing");
		CHECK(fixture.Get(id).GetTransform().Position == glm::vec3(0.0f));

		// Starting paused, for deterministic playtests
		CHECK(fixture.Ok("play_start", {{"paused", true}}) ==
			Json::Value{{"state", "Paused"}, {"tick", 0}, {"queuedInput", 0}});
		fixture.Context.Update(1.0);
		CHECK(fixture.Ok("play_status")["tick"] == 0);
		fixture.Ok("play_stop");
		fixture.Fails("play_start", {{"paused", "yes"}});
		CHECK(fixture.Context.GetPlayState() == PlayState::Editing);
	}

	TEST_CASE("input_send queues held, tapped and clicked input for the coming ticks")
	{
		Fixture fixture;
		std::vector<InputCommand> received;
		fixture.Context.AddPlaySystem(
			"Record", [&received](SimulationContext& context) { received.push_back(context.GetInput(0)); });
		fixture.Fails("input_send", {{"hold", {"W"}}});
		fixture.Ok("play_start");
		fixture.Ok("play_pause");

		const Json::Value queued = fixture.Ok("input_send",
			{{"hold", {"W", "LeftShift"}}, {"tap", {"Space"}}, {"mouseTap", {"Left"}}, {"mouseHold", {"Right"}},
				{"ticks", 3}});
		// Three ticks held, then the release
		CHECK(queued["queuedInput"] == 4);
		fixture.Ok("play_step", {{"ticks", 5}});

		REQUIRE(received.size() == 5);
		const auto w = std::to_underlying(Key::W);
		const auto space = std::to_underlying(Key::Space);
		const auto left = std::to_underlying(MouseButton::Left);
		const auto right = std::to_underlying(MouseButton::Right);
		CHECK(received[0].KeysDown.test(w));
		CHECK(received[0].KeysPressed.test(w));
		CHECK(received[0].KeysPressed.test(space));
		CHECK(received[0].KeysReleased.test(space));
		CHECK_FALSE(received[0].KeysDown.test(space));
		CHECK(received[0].MouseButtonsPressed.test(left));
		CHECK(received[0].MouseButtonsReleased.test(left));
		CHECK(received[0].MouseButtonsDown.test(right));
		CHECK(received[1].KeysDown.test(w));
		CHECK_FALSE(received[1].KeysPressed.test(w));
		CHECK(received[2].KeysDown.test(w));
		CHECK(received[3].KeysReleased.test(w));
		CHECK(received[3].MouseButtonsReleased.test(right));
		CHECK_FALSE(received[3].KeysDown.test(w));
		CHECK(received[4].KeysDown.none());

		fixture.Fails("input_send", {{"hold", {"NotAKey"}}});
		fixture.Fails("input_send", {{"mouseTap", {"Thumb"}}});
		fixture.Fails("input_send", {{"tap", "Space"}});
		fixture.Fails("input_send", {{"ticks", 0}});
		CHECK(fixture.Ok("play_status")["queuedInput"] == 0);
	}

	TEST_CASE("viewport_screenshot returns a PNG of the requested size")
	{
		Fixture fixture;

		const McpToolResult result = fixture.Call("viewport_screenshot", {{"width", 64}, {"height", 32}});

		REQUIRE_FALSE(result.IsError);
		REQUIRE(result.Content.size() == 2);
		CHECK(result.Content[0]["text"] == "The viewport, 64x32 pixels");
		CHECK(result.Content[1]["type"] == "image");
		CHECK(result.Content[1]["mimeType"] == "image/png");
		const std::vector<uint8_t> png = DecodeBase64(result.Content[1]["data"].get<std::string>());
		const auto image = Image::Decode(png);
		REQUIRE_MESSAGE(image.has_value(), Testing::DescribeError(image));
		CHECK(image->GetWidth() == 64);
		CHECK(image->GetHeight() == 32);
		CHECK(image->GetPixel(5, 5) == Rgba8{.R = 10, .G = 20, .B = 30, .A = 255});
		CHECK(fixture.CaptureCount == 1);
	}

	TEST_CASE("viewport_screenshot defaults to 1280x720 and limits the size")
	{
		Fixture fixture;

		CHECK(fixture.Call("viewport_screenshot").Content[0]["text"] == "The viewport, 1280x720 pixels");
		fixture.Fails("viewport_screenshot", {{"width", 15}});
		fixture.Fails("viewport_screenshot", {{"height", 4097}});
		fixture.Fails("viewport_screenshot", {{"width", 100.5}});
		CHECK(fixture.CaptureCount == 1);
	}

	TEST_CASE("viewport_screenshot reports when nothing can render")
	{
		Fixture fixture;
		fixture.CaptureFails = true;
		CHECK(fixture.Fails("viewport_screenshot").contains("No GPU today"));

		EditorContext context;
		McpServer server("Lodestone Editor", "0.0.0", "");
		RegisterEditorTools(server, context, EditorToolHost());
		const McpToolResult result = server.CallTool("viewport_screenshot", Json::Value::object());
		CHECK(result.IsError);
		CHECK(result.Content[0]["text"].get<std::string>().contains("graphics device"));
	}

	TEST_CASE("camera_set and camera_frame aim the editor camera")
	{
		Fixture fixture;
		const std::string id =
			fixture.CreateEntity("Target", Json::Value(), {{"Transform", {{"Position", {5.0, 0.0, 0.0}}}}});

		fixture.Ok("camera_set", {{"position", {0.0, 10.0, 10.0}}, {"target", {0.0, 0.0, 0.0}}});
		const EditorCamera& camera = fixture.Context.GetCamera();
		CHECK(glm::length(camera.GetPosition() - glm::vec3(0.0f, 10.0f, 10.0f)) < 1e-3f);
		fixture.Fails("camera_set", {{"position", {1.0, 1.0, 1.0}}, {"target", {1.0, 1.0, 1.0}}});
		fixture.Fails("camera_set", {{"position", {1.0, 1.0}}, {"target", {0.0, 0.0, 0.0}}});

		fixture.Ok("camera_frame", {{"entity", id}});
		CHECK(glm::length(camera.GetTarget() - glm::vec3(5.0f, 0.0f, 0.0f)) < 1e-3f);
		CHECK(glm::length(camera.GetPosition() - glm::vec3(5.0f, 10.0f, 10.0f)) < 1e-3f);
		fixture.Fails("camera_frame", {{"entity", UUID::Generate().ToString()}});
	}

	TEST_CASE("log_read returns recent messages, newer than a sequence number and at a level")
	{
		Fixture fixture;
		const uint64_t start = fixture.Ok("log_read")["latest"].get<uint64_t>();
		LS_CORE_INFO("Tool test info");
		LS_CORE_ERROR("Tool test error");

		const Json::Value all = fixture.Ok("log_read", {{"after", start}});
		REQUIRE(all["entries"].size() == 2);
		CHECK(all["entries"][0]["message"] == "Tool test info");
		CHECK(all["entries"][0]["level"] == "info");
		CHECK(all["entries"][1]["level"] == "error");
		CHECK(all["entries"][1]["logger"] == "Engine");
		CHECK(all["latest"] == all["entries"][1]["sequence"]);

		const Json::Value errors = fixture.Ok("log_read", {{"after", start}, {"level", "error"}});
		REQUIRE(errors["entries"].size() == 1);
		CHECK(fixture.Ok("log_read", {{"after", start}, {"max", 1}})["entries"][0]["message"] == "Tool test error");
		fixture.Fails("log_read", {{"level", "verbose"}});
		fixture.Fails("log_read", {{"max", 1001}});
	}

	TEST_CASE("asset_import copies files into the project's assets and registers them")
	{
		Fixture fixture;
		const std::filesystem::path source = fixture.Directory.GetPath() / "Brick.png";
		REQUIRE(Image(4, 4).SavePng(source).has_value());

		const Json::Value imported =
			fixture.Ok("asset_import", {{"source", PathToUtf8(source)}, {"path", "Textures/Brick.png"}});
		CHECK(imported["path"] == "Textures/Brick.png");
		CHECK(UUID::Parse(imported["id"].get<std::string>()).has_value());
		CHECK(std::filesystem::is_regular_file(fixture.GetAssetDirectory() / "Textures" / "Brick.png"));

		fixture.Fails("asset_import", {{"source", PathToUtf8(source)}, {"path", "Textures/Brick.png"}});
		CHECK(fixture.Ok("asset_import",
				  {{"source", PathToUtf8(source)}, {"path", "Textures/Brick.png"}, {"overwrite", true}})["id"] ==
			imported["id"]);
		fixture.Fails("asset_import", {{"source", PathToUtf8(source)}, {"path", "../Brick.png"}});
		fixture.Fails("asset_import", {{"source", PathToUtf8(source)}, {"path", "Brick.unknown"}});
		fixture.Fails("asset_import",
			{{"source", PathToUtf8(fixture.Directory.GetPath() / "Missing.png")}, {"path", "Missing.png"}});
	}

	TEST_CASE("asset_list lists assets, optionally of one type, and asset_rescan finds new ones")
	{
		Fixture fixture;
		fixture.CreateEntity("A");
		fixture.Ok("document_save", {{"path", "Scenes/Main.lscene"}});
		REQUIRE(Image(2, 2).SavePng(fixture.GetAssetDirectory() / "Icon.png").has_value());

		CHECK(fixture.Ok("asset_list")["assets"].size() == 1);
		const Json::Value rescan = fixture.Ok("asset_rescan");
		CHECK(rescan["assetCount"] == 2);
		CHECK(rescan["created"] == Json::Value{"Icon.png"});
		CHECK(rescan["errors"].empty());

		const Json::Value all = fixture.Ok("asset_list")["assets"];
		REQUIRE(all.size() == 2);
		CHECK(all[0]["path"] == "Icon.png");
		CHECK(all[0]["type"] == "Texture");
		CHECK(all[1]["path"] == "Scenes/Main.lscene");
		const Json::Value scenes = fixture.Ok("asset_list", {{"type", "Scene"}})["assets"];
		REQUIRE(scenes.size() == 1);
		CHECK(scenes[0]["type"] == "Scene");
		fixture.Fails("asset_list", {{"type", "Mesh"}});
	}

	TEST_CASE("Asset tools need a project")
	{
		Fixture fixture(false);

		fixture.Fails("asset_list");
		fixture.Fails("asset_rescan");
		fixture.Fails("asset_import", {{"source", "a.png"}, {"path", "a.png"}});
		fixture.Fails("document_save", {{"path", "Main.lscene"}});
		fixture.Fails("prefab_instantiate", {{"path", "A.lprefab"}});
	}

	TEST_CASE("editor_quit closes the editor, guarding unsaved changes")
	{
		Fixture fixture;
		fixture.CreateEntity("Unsaved");

		CHECK(fixture.Fails("editor_quit").contains("unsaved"));
		CHECK(fixture.QuitRequests == 0);
		CHECK(fixture.Ok("editor_quit", {{"discardChanges", true}}) == "The editor is closing");
		CHECK(fixture.QuitRequests == 1);
	}

	TEST_CASE("editor_quit fails where the editor closes another way")
	{
		EditorContext context;
		McpServer server("Lodestone Editor", "0.0.0", "");
		RegisterEditorTools(server, context, EditorToolHost());

		const McpToolResult result = server.CallTool("editor_quit", Json::Value::object());

		CHECK(result.IsError);
		CHECK(result.Content[0]["text"].get<std::string>().contains("standard input"));
	}

}
