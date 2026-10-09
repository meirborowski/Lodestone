#include "Lodestone/Editor/Mcp/EditorTools.h"

#include "Lodestone/Core/Base64.h"
#include "Lodestone/Core/FileSystem.h"
#include "Lodestone/Editor/Commands/EntityCommands.h"
#include "Lodestone/Input/InputCodes.h"
#include "Lodestone/Scene/Entity.h"

#include <glm/vec4.hpp>
#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/std.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace Lodestone {

	namespace {

		constexpr uint32_t MaxTicksPerCall = 10000;
		constexpr uint32_t MaxScreenshotSize = 4096;
		constexpr uint32_t MinScreenshotSize = 16;
		constexpr size_t MaxLogEntries = 1000;

		McpToolResult Fail(const Error& error)
		{
			return McpToolResult::Failure(error.GetMessageText());
		}

		Error ArgumentError(std::string message)
		{
			return Error(ErrorCode::InvalidArgument, std::move(message));
		}

		// The schema of a tool's arguments, from JSON text written in this file
		Json::Value Schema(std::string_view text)
		{
			auto schema = Json::Parse(text);
			LS_CORE_ASSERT(schema.has_value(), "A tool's input schema isn't valid JSON");
			return schema ? std::move(*schema) : Json::Value::object();
		}

		// Typed access to a tool's arguments; every error names the argument
		class Arguments
		{
		public:
			explicit Arguments(const Json::Value& json)
				: m_Json(&json)
			{
			}

			bool Has(std::string_view key) const
			{
				return m_Json->contains(std::string(key)) && !(*m_Json)[std::string(key)].is_null();
			}

			std::expected<std::string, Error> String(std::string_view key) const
			{
				if (!Has(key))
					return std::unexpected(ArgumentError(fmt::format("'{}' is required", key)));
				const Json::Value& value = (*m_Json)[std::string(key)];
				if (!value.is_string())
					return std::unexpected(ArgumentError(fmt::format("'{}' must be a string", key)));
				return value.get<std::string>();
			}

			std::expected<std::optional<std::string>, Error> OptionalString(std::string_view key) const
			{
				if (!Has(key))
					return std::nullopt;
				return String(key);
			}

			std::expected<UUID, Error> Id(std::string_view key) const
			{
				const auto text = String(key);
				if (!text)
					return std::unexpected(text.error());
				const std::optional<UUID> id = UUID::Parse(*text);
				if (!id)
					return std::unexpected(ArgumentError(fmt::format("'{}' must be a UUID, not '{}'", key, *text)));
				return *id;
			}

			// The nil UUID when absent or null - which means "the root" for parents
			std::expected<UUID, Error> OptionalId(std::string_view key) const
			{
				if (!Has(key))
					return UUID();
				return Id(key);
			}

			std::expected<uint32_t, Error> UInt(
				std::string_view key, uint32_t fallback, uint32_t min, uint32_t max) const
			{
				if (!Has(key))
					return fallback;
				const Json::Value& value = (*m_Json)[std::string(key)];
				if (!value.is_number_integer() || value.get<int64_t>() < min || value.get<int64_t>() > max)
					return std::unexpected(
						ArgumentError(fmt::format("'{}' must be a whole number from {} to {}", key, min, max)));
				return static_cast<uint32_t>(value.get<int64_t>());
			}

			std::expected<std::optional<size_t>, Error> OptionalIndex(std::string_view key) const
			{
				if (!Has(key))
					return std::nullopt;
				const auto index = UInt(key, 0, 0, std::numeric_limits<int32_t>::max());
				if (!index)
					return std::unexpected(index.error());
				return static_cast<size_t>(*index);
			}

			std::expected<bool, Error> Bool(std::string_view key, bool fallback) const
			{
				if (!Has(key))
					return fallback;
				const Json::Value& value = (*m_Json)[std::string(key)];
				if (!value.is_boolean())
					return std::unexpected(ArgumentError(fmt::format("'{}' must be true or false", key)));
				return value.get<bool>();
			}

			std::expected<Json::Value, Error> Object(std::string_view key) const
			{
				if (!Has(key))
					return Json::Value::object();
				const Json::Value& value = (*m_Json)[std::string(key)];
				if (!value.is_object())
					return std::unexpected(ArgumentError(fmt::format("'{}' must be an object", key)));
				return value;
			}

			std::expected<std::vector<std::string>, Error> Strings(std::string_view key) const
			{
				std::vector<std::string> strings;
				if (!Has(key))
					return strings;
				const Json::Value& value = (*m_Json)[std::string(key)];
				if (!value.is_array())
					return std::unexpected(ArgumentError(fmt::format("'{}' must be an array of strings", key)));
				for (const Json::Value& element : value)
				{
					if (!element.is_string())
						return std::unexpected(ArgumentError(fmt::format("'{}' must be an array of strings", key)));
					strings.push_back(element.get<std::string>());
				}
				return strings;
			}

			std::expected<glm::vec3, Error> Vec3(std::string_view key) const
			{
				if (!Has(key))
					return std::unexpected(ArgumentError(fmt::format("'{}' is required", key)));
				auto value = Json::ToFieldValue((*m_Json)[std::string(key)], FieldType::Vec3, key);
				if (!value)
					return std::unexpected(ArgumentError(value.error().GetMessageText()));
				return std::get<glm::vec3>(*value);
			}

		private:
			const Json::Value* m_Json;
		};

		Json::Value IdOrNull(UUID id)
		{
			return id.IsNil() ? Json::Value() : Json::Value(id.ToString());
		}

		// An entity's components as {"Type": {"Field": value}}, internal ones left out
		Json::Value DescribeComponents(const Scene& scene, entt::entity entity)
		{
			Json::Value components = Json::Value::object();
			for (const ComponentType* type : scene.GetComponentRegistry().GetTypes())
			{
				if (type->IsInternal())
					continue;
				const void* component = type->TryGet(scene.GetRegistry(), entity);
				if (component == nullptr)
					continue;
				Json::Value fields = Json::Value::object();
				for (const FieldInfo& field : type->GetFields())
					fields[field.GetName()] = Json::FromFieldValue(field.Get(component));
				components[type->GetName()] = std::move(fields);
			}
			return components;
		}

		Json::Value DescribeEntity(Scene& scene, Entity entity)
		{
			Json::Value children = Json::Value::array();
			for (const UUID child : entity.Get<HierarchyComponent>().Children)
				children.push_back(child.ToString());
			Json::Value json = Json::Value::object();
			json["id"] = entity.GetId().ToString();
			json["name"] = entity.GetName();
			json["parent"] = IdOrNull(entity.Get<HierarchyComponent>().Parent);
			json["children"] = std::move(children);
			const glm::mat4 world = scene.GetWorldMatrix(entity);
			json["worldPosition"] = Json::FromFieldValue(glm::vec3(world[3]));
			json["components"] = DescribeComponents(scene, entity.GetHandle());
			return json;
		}

		Json::Value DescribeHierarchy(const Scene& scene)
		{
			Json::Value entities = Json::Value::array();
			// Depth-first without recursion, so deep hierarchies can't overflow the stack
			std::vector<std::pair<UUID, uint32_t>> pending;
			for (const UUID root : std::views::reverse(scene.GetRootEntities()))
				pending.emplace_back(root, 0);
			while (!pending.empty())
			{
				const auto [id, depth] = pending.back();
				pending.pop_back();
				const entt::entity handle = scene.FindHandle(id);
				const auto& hierarchy = scene.GetRegistry().get<HierarchyComponent>(handle);
				Json::Value json = Json::Value::object();
				json["id"] = id.ToString();
				json["name"] = scene.GetRegistry().get<NameComponent>(handle).Name;
				json["parent"] = IdOrNull(hierarchy.Parent);
				json["depth"] = depth;
				entities.push_back(std::move(json));
				for (const UUID child : std::views::reverse(hierarchy.Children))
					pending.emplace_back(child, depth + 1);
			}
			return entities;
		}

		Json::Value DescribeComponentTypes(const ComponentRegistry& registry)
		{
			Json::Value types = Json::Value::array();
			for (const ComponentType* type : registry.GetTypes())
			{
				if (type->IsInternal())
					continue;
				Json::Value fields = Json::Value::array();
				for (const FieldInfo& field : type->GetFields())
				{
					Json::Value json = Json::Value::object();
					json["name"] = field.GetName();
					json["type"] = ToString(field.GetType());
					json["description"] = field.GetDescription();
					json["default"] = Json::FromFieldValue(field.GetDefault());
					if (field.GetMin())
						json["min"] = *field.GetMin();
					if (field.GetMax())
						json["max"] = *field.GetMax();
					json["readOnly"] = field.IsReadOnly();
					json["replicated"] = field.IsReplicated();
					fields.push_back(std::move(json));
				}
				Json::Value json = Json::Value::object();
				json["name"] = type->GetName();
				json["description"] = type->GetDescription();
				json["required"] = type->IsRequired();
				json["fields"] = std::move(fields);
				types.push_back(std::move(json));
			}
			return types;
		}

		// Documents with unsaved changes are only replaced on request, so an agent can't lose work by accident
		std::expected<void, Error> CheckUnsaved(const EditorContext& context, const Arguments& arguments)
		{
			const auto discard = arguments.Bool("discardChanges", false);
			if (!discard)
				return std::unexpected(discard.error());
			if (context.HasUnsavedChanges() && !*discard)
				return std::unexpected(Error(ErrorCode::InvalidState,
					"The open document has unsaved changes. Save it with document_save, or pass discardChanges: true"));
			return {};
		}

		std::expected<LogLevel, Error> ParseLogLevel(std::string_view name)
		{
			constexpr std::array names = {std::pair{std::string_view("trace"), LogLevel::Trace},
				std::pair{std::string_view("debug"), LogLevel::Debug},
				std::pair{std::string_view("info"), LogLevel::Info},
				std::pair{std::string_view("warn"), LogLevel::Warn},
				std::pair{std::string_view("error"), LogLevel::Error},
				std::pair{std::string_view("critical"), LogLevel::Critical}};
			for (const auto& [levelName, level] : names)
			{
				if (levelName == name)
					return level;
			}
			return std::unexpected(ArgumentError(
				fmt::format("'{}' isn't a log level: use trace, debug, info, warn, error or critical", name)));
		}

		std::string_view ToLowerName(LogLevel level)
		{
			switch (level)
			{
				case LogLevel::Trace:
					return "trace";
				case LogLevel::Debug:
					return "debug";
				case LogLevel::Info:
					return "info";
				case LogLevel::Warn:
					return "warn";
				case LogLevel::Error:
					return "error";
				case LogLevel::Critical:
					return "critical";
				case LogLevel::Off:
					return "off";
			}
			return "unknown";
		}

		// Runs a command and describes the outcome. The result names what changed, for the agent's next step
		McpToolResult Run(EditorContext& context, Scope<Command> command, Json::Value result = Json::Value::object())
		{
			if (auto executed = context.Execute(std::move(command)); !executed)
				return Fail(executed.error());
			// Named once it's run, since commands learn what they change as they run
			result["done"] = context.GetHistory().GetUndoName().value_or("");
			return McpToolResult::Structured(std::move(result));
		}

		void AddProjectTools(McpServer& server, EditorContext& context, std::function<void()> requestQuit)
		{
			server.AddTool({.Name = "project_info",
				.Title = "Project and editor state",
				.Description = "The open project, the open document (scene or prefab), whether it has unsaved changes, "
							   "the play state, the selection and the undo state",
				.InputSchema = Schema(R"({"type": "object", "properties": {}})"),
				.ReadOnly = true,
				.Handler = [&context](const Json::Value&)
				{
					Json::Value info = Json::Value::object();
					if (const Project* project = context.GetProject())
					{
						info["project"] = {{"name", project->GetSettings().Name},
							{"directory", PathToUtf8(project->GetDirectory())},
							{"assetDirectory", PathToUtf8(project->GetAssetDirectory())},
							{"tickRate", project->GetSettings().TickRate}};
					}
					else
					{
						info["project"] = nullptr;
					}
					const auto& path = context.GetDocumentPath();
					info["document"] = {
						{"kind", context.GetDocumentKind() == EditorContext::DocumentKind::Scene ? "scene" : "prefab"},
						{"path", path ? Json::Value(*path) : Json::Value()},
						{"unsavedChanges", context.HasUnsavedChanges()},
						{"entityCount", context.GetScene().GetEntityCount()}};
					info["playState"] = ToString(context.GetPlayState());
					info["selection"] = IdOrNull(context.GetSelection());
					info["canUndo"] = context.GetHistory().CanUndo();
					info["canRedo"] = context.GetHistory().CanRedo();
					return McpToolResult::Structured(std::move(info));
				}});

			server.AddTool({.Name = "project_create",
				.Title = "Create a project",
				.Description = "Creates a game project - a project file and an Assets directory - in a directory that "
							   "has none, and opens it with a new, empty scene",
				.InputSchema = Schema(R"({"type": "object", "properties": {
					"directory": {"type": "string", "description": "The project's directory, created if needed"},
					"name": {"type": "string", "description": "The game's name"},
					"discardChanges": {"type": "boolean", "description": "Discard unsaved changes to the open document"}},
					"required": ["directory", "name"]})"),
				.Handler = [&context](const Json::Value& json)
				{
					const Arguments arguments(json);
					const auto directory = arguments.String("directory");
					const auto name = arguments.String("name");
					if (!directory)
						return Fail(directory.error());
					if (!name)
						return Fail(name.error());
					if (auto checked = CheckUnsaved(context, arguments); !checked)
						return Fail(checked.error());
					if (auto created = context.CreateProject(PathFromUtf8(*directory), *name); !created)
						return Fail(created.error());
					return McpToolResult::Text(fmt::format("Created and opened project '{}'", *name));
				}});

			server.AddTool({.Name = "project_open",
				.Title = "Open a project",
				.Description = "Opens the project in a directory, with a new, empty scene",
				.InputSchema = Schema(R"({"type": "object", "properties": {
					"directory": {"type": "string", "description": "The directory with Project.lsproject"},
					"discardChanges": {"type": "boolean", "description": "Discard unsaved changes to the open document"}},
					"required": ["directory"]})"),
				.Handler = [&context](const Json::Value& json)
				{
					const Arguments arguments(json);
					const auto directory = arguments.String("directory");
					if (!directory)
						return Fail(directory.error());
					if (auto checked = CheckUnsaved(context, arguments); !checked)
						return Fail(checked.error());
					if (auto opened = context.OpenProject(PathFromUtf8(*directory)); !opened)
						return Fail(opened.error());
					return McpToolResult::Text(
						fmt::format("Opened project '{}'", context.GetProject()->GetSettings().Name));
				}});

			server.AddTool({.Name = "editor_quit",
				.Title = "Close the editor",
				.Description = "Closes the editor once this request is answered. The stdio editor closes when its "
							   "standard input does instead",
				.InputSchema = Schema(R"({"type": "object", "properties": {
					"discardChanges": {"type": "boolean", "description": "Discard unsaved changes to the open document"}}})"),
				.Destructive = true,
				.Handler = [&context, requestQuit = std::move(requestQuit)](const Json::Value& json)
				{
					if (!requestQuit)
						return McpToolResult::Failure(
							"This editor closes when its MCP client closes its standard input");
					if (auto checked = CheckUnsaved(context, Arguments(json)); !checked)
						return Fail(checked.error());
					requestQuit();
					return McpToolResult::Text("The editor is closing");
				}});
		}

		void AddDocumentTools(McpServer& server, EditorContext& context)
		{
			server.AddTool({.Name = "scene_new",
				.Title = "New scene",
				.Description = "Replaces the open document with a new, empty scene",
				.InputSchema = Schema(R"({"type": "object", "properties": {
					"discardChanges": {"type": "boolean", "description": "Discard unsaved changes to the open document"}}})"),
				.Handler = [&context](const Json::Value& json)
				{
					if (auto checked = CheckUnsaved(context, Arguments(json)); !checked)
						return Fail(checked.error());
					if (auto created = context.NewScene(); !created)
						return Fail(created.error());
					return McpToolResult::Text("Started a new scene");
				}});

			const auto addOpenTool = [&server, &context](
										 std::string name, std::string title, std::string description, bool prefab)
			{
				server.AddTool({.Name = std::move(name),
					.Title = std::move(title),
					.Description = std::move(description),
					.InputSchema = Schema(R"({"type": "object", "properties": {
						"path": {"type": "string", "description": "Relative to the project's Assets directory"},
						"discardChanges": {"type": "boolean", "description": "Discard unsaved changes to the open document"}},
						"required": ["path"]})"),
					.Handler = [&context, prefab](const Json::Value& json)
					{
						const Arguments arguments(json);
						const auto path = arguments.String("path");
						if (!path)
							return Fail(path.error());
						if (auto checked = CheckUnsaved(context, arguments); !checked)
							return Fail(checked.error());
						if (auto opened = prefab ? context.OpenPrefab(*path) : context.OpenScene(*path); !opened)
							return Fail(opened.error());
						return McpToolResult::Text(fmt::format("Opened {}", *path));
					}});
			};
			addOpenTool("scene_open", "Open a scene", "Opens a scene (.lscene) from the project's assets", false);
			addOpenTool("prefab_open", "Open a prefab for editing",
				"Opens a prefab (.lprefab) as the document, to edit it: its root is the only root entity. Save it with "
				"document_save. Instances made before keep their copy",
				true);

			server.AddTool({.Name = "document_save",
				.Title = "Save the document",
				.Description = "Saves the open scene (.lscene) or prefab (.lprefab), to its own path or a new one",
				.InputSchema = Schema(R"({"type": "object", "properties": {
					"path": {"type": "string", "description": "Relative to the project's Assets directory. Needed the first time"}}})"),
				.Handler = [&context](const Json::Value& json)
				{
					const auto path = Arguments(json).OptionalString("path");
					if (!path)
						return Fail(path.error());
					if (auto saved =
							context.SaveDocument(*path ? std::optional<std::string_view>(**path) : std::nullopt);
						!saved)
						return Fail(saved.error());
					return McpToolResult::Text(fmt::format("Saved {}", *context.GetDocumentPath()));
				}});

			server.AddTool({.Name = "scene_hierarchy",
				.Title = "Scene hierarchy",
				.Description = "Every entity of the open document in hierarchy order - parents before their "
							   "children - with its UUID, name, parent and depth",
				.InputSchema = Schema(R"({"type": "object", "properties": {}})"),
				.ReadOnly = true,
				.Handler = [&context](const Json::Value&)
				{ return McpToolResult::Structured({{"entities", DescribeHierarchy(context.GetScene())}}); }});
		}

		void AddEntityTools(McpServer& server, EditorContext& context)
		{
			server.AddTool({.Name = "entity_create",
				.Title = "Create an entity",
				.Description =
					"Creates an entity, optionally under a parent and with components. Component data has the "
					"form {\"Transform\": {\"Position\": [0, 1, 0]}}; see component_types for what exists. Returns "
					"the entity's UUID",
				.InputSchema = Schema(R"({"type": "object", "properties": {
					"name": {"type": "string"},
					"parent": {"type": ["string", "null"], "description": "The parent's UUID; absent or null for a root entity"},
					"index": {"type": "integer", "minimum": 0, "description": "Position among the siblings; the end by default"},
					"components": {"type": "object", "description": "Components to add or set, with field values"}}})"),
				.Handler = [&context](const Json::Value& json)
				{
					const Arguments arguments(json);
					const auto name = arguments.OptionalString("name");
					const auto parent = arguments.OptionalId("parent");
					const auto index = arguments.OptionalIndex("index");
					const auto components = arguments.Object("components");
					if (!name)
						return Fail(name.error());
					if (!parent)
						return Fail(parent.error());
					if (!index)
						return Fail(index.error());
					if (!components)
						return Fail(components.error());
					auto command =
						CreateScope<CreateEntityCommand>(name->value_or("Entity"), *parent, *index, *components);
					const CreateEntityCommand& created = *command;
					McpToolResult result = Run(context, std::move(command));
					if (!result.IsError)
					{
						Json::Value data = *result.StructuredContent;
						data["id"] = created.GetEntityId().ToString();
						result = McpToolResult::Structured(std::move(data));
					}
					return result;
				}});

			server.AddTool({.Name = "entity_delete",
				.Title = "Delete an entity",
				.Description = "Deletes an entity and all its descendants (undoable)",
				.InputSchema =
					Schema(R"({"type": "object", "properties": {"entity": {"type": "string", "description": "UUID"}},
					"required": ["entity"]})"),
				.Destructive = true,
				.Handler = [&context](const Json::Value& json)
				{
					const auto id = Arguments(json).Id("entity");
					if (!id)
						return Fail(id.error());
					return Run(context, CreateScope<DestroyEntityCommand>(*id));
				}});

			server.AddTool({.Name = "entity_duplicate",
				.Title = "Duplicate an entity",
				.Description =
					"Copies an entity and its descendants, with new UUIDs, next to the original. Returns the "
					"copy's UUID",
				.InputSchema =
					Schema(R"({"type": "object", "properties": {"entity": {"type": "string", "description": "UUID"}},
					"required": ["entity"]})"),
				.Handler = [&context](const Json::Value& json)
				{
					const auto id = Arguments(json).Id("entity");
					if (!id)
						return Fail(id.error());
					auto command = CreateScope<DuplicateEntityCommand>(*id);
					const DuplicateEntityCommand& duplicate = *command;
					McpToolResult result = Run(context, std::move(command));
					if (!result.IsError)
					{
						Json::Value data = *result.StructuredContent;
						data["id"] = duplicate.GetCopyId().ToString();
						result = McpToolResult::Structured(std::move(data));
					}
					return result;
				}});

			server.AddTool({.Name = "entity_set_parent",
				.Title = "Move an entity in the hierarchy",
				.Description = "Moves an entity under a new parent, or to the root, at a position among its siblings. "
							   "Its local transform is kept",
				.InputSchema = Schema(R"({"type": "object", "properties": {
					"entity": {"type": "string", "description": "UUID"},
					"parent": {"type": ["string", "null"], "description": "The new parent's UUID; absent or null for the root"},
					"index": {"type": "integer", "minimum": 0, "description": "Position among the siblings; the end by default"}},
					"required": ["entity"]})"),
				.Handler = [&context](const Json::Value& json)
				{
					const Arguments arguments(json);
					const auto id = arguments.Id("entity");
					const auto parent = arguments.OptionalId("parent");
					const auto index = arguments.OptionalIndex("index");
					if (!id)
						return Fail(id.error());
					if (!parent)
						return Fail(parent.error());
					if (!index)
						return Fail(index.error());
					return Run(context, CreateScope<SetParentCommand>(*id, *parent, *index));
				}});

			server.AddTool({.Name = "entity_get",
				.Title = "Get an entity",
				.Description = "An entity's name, parent, children, world position and every component with its fields",
				.InputSchema =
					Schema(R"({"type": "object", "properties": {"entity": {"type": "string", "description": "UUID"}},
					"required": ["entity"]})"),
				.ReadOnly = true,
				.Handler = [&context](const Json::Value& json)
				{
					const auto id = Arguments(json).Id("entity");
					if (!id)
						return Fail(id.error());
					const Entity entity = context.GetScene().FindEntity(*id);
					if (!entity)
						return McpToolResult::Failure(fmt::format("The scene has no entity {}", *id));
					return McpToolResult::Structured(DescribeEntity(context.GetScene(), entity));
				}});

			server.AddTool({.Name = "entity_find",
				.Title = "Find entities by name",
				.Description = "The entities whose name matches, in hierarchy order",
				.InputSchema = Schema(R"({"type": "object", "properties": {
					"name": {"type": "string"},
					"contains": {"type": "boolean", "description": "Match names that contain the text, ignoring case, instead of exactly"}},
					"required": ["name"]})"),
				.ReadOnly = true,
				.Handler = [&context](const Json::Value& json)
				{
					const Arguments arguments(json);
					const auto name = arguments.String("name");
					const auto contains = arguments.Bool("contains", false);
					if (!name)
						return Fail(name.error());
					if (!contains)
						return Fail(contains.error());
					const auto lower = [](std::string text)
					{
						std::ranges::transform(text, text.begin(),
							[](char character)
							{
								return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a')
																			: character;
							});
						return text;
					};
					const std::string needle = lower(*name);
					Json::Value matches = Json::Value::array();
					for (const Json::Value& entity : DescribeHierarchy(context.GetScene()))
					{
						const auto entityName = entity["name"].get<std::string>();
						if (*contains ? lower(entityName).contains(needle) : entityName == *name)
							matches.push_back({{"id", entity["id"]}, {"name", entityName}});
					}
					return McpToolResult::Structured({{"entities", std::move(matches)}});
				}});

			server.AddTool({.Name = "selection_set",
				.Title = "Select an entity",
				.Description = "Selects an entity in the editor, or clears the selection",
				.InputSchema = Schema(R"({"type": "object", "properties": {
					"entity": {"type": ["string", "null"], "description": "UUID; absent or null to clear the selection"}}})"),
				.Handler = [&context](const Json::Value& json)
				{
					const auto id = Arguments(json).OptionalId("entity");
					if (!id)
						return Fail(id.error());
					if (!id->IsNil() && !context.GetScene().Contains(*id))
						return McpToolResult::Failure(fmt::format("The scene has no entity {}", *id));
					context.Select(*id);
					return McpToolResult::Text(id->IsNil() ? "Cleared the selection" : "Selected the entity");
				}});
		}

		void AddComponentTools(McpServer& server, EditorContext& context)
		{
			server.AddTool({.Name = "component_types",
				.Title = "Component schemas",
				.Description =
					"Every component type that can be added or edited, with its fields: type, description, "
					"default, limits and flags. Required components exist on every entity and can't be removed",
				.InputSchema = Schema(R"({"type": "object", "properties": {}})"),
				.ReadOnly = true,
				.Handler = [&context](const Json::Value&)
				{
					return McpToolResult::Structured(
						{{"components", DescribeComponentTypes(context.GetScene().GetComponentRegistry())}});
				}});

			server.AddTool({.Name = "component_add",
				.Title = "Add a component",
				.Description = "Adds a component to an entity, with field values; fields not given keep their defaults",
				.InputSchema = Schema(R"({"type": "object", "properties": {
					"entity": {"type": "string", "description": "UUID"},
					"component": {"type": "string", "description": "The component type's name, from component_types"},
					"fields": {"type": "object", "description": "Field values, such as {\"Speed\": 2.5}"}},
					"required": ["entity", "component"]})"),
				.Handler = [&context](const Json::Value& json)
				{
					const Arguments arguments(json);
					const auto id = arguments.Id("entity");
					const auto component = arguments.String("component");
					const auto fields = arguments.Object("fields");
					if (!id)
						return Fail(id.error());
					if (!component)
						return Fail(component.error());
					if (!fields)
						return Fail(fields.error());
					return Run(context, CreateScope<AddComponentCommand>(*id, *component, *fields));
				}});

			server.AddTool({.Name = "component_remove",
				.Title = "Remove a component",
				.Description = "Removes a component that isn't required from an entity (undoable)",
				.InputSchema = Schema(R"({"type": "object", "properties": {
					"entity": {"type": "string", "description": "UUID"},
					"component": {"type": "string"}},
					"required": ["entity", "component"]})"),
				.Destructive = true,
				.Handler = [&context](const Json::Value& json)
				{
					const Arguments arguments(json);
					const auto id = arguments.Id("entity");
					const auto component = arguments.String("component");
					if (!id)
						return Fail(id.error());
					if (!component)
						return Fail(component.error());
					return Run(context, CreateScope<RemoveComponentCommand>(*id, *component));
				}});

			server.AddTool({.Name = "component_set",
				.Title = "Set component fields",
				.Description = "Sets fields of an entity's component, such as {\"Position\": [1, 0, 2]} for Transform. "
							   "Vectors are arrays, rotations are quaternions [x, y, z, w], UUIDs are strings",
				.InputSchema = Schema(R"({"type": "object", "properties": {
					"entity": {"type": "string", "description": "UUID"},
					"component": {"type": "string"},
					"fields": {"type": "object"}},
					"required": ["entity", "component", "fields"]})"),
				.Handler = [&context](const Json::Value& json)
				{
					const Arguments arguments(json);
					const auto id = arguments.Id("entity");
					const auto component = arguments.String("component");
					const auto fields = arguments.Object("fields");
					if (!id)
						return Fail(id.error());
					if (!component)
						return Fail(component.error());
					if (!fields)
						return Fail(fields.error());
					return Run(context, CreateScope<SetFieldsCommand>(*id, *component, *fields));
				}});
		}

		void AddPrefabTools(McpServer& server, EditorContext& context)
		{
			server.AddTool({.Name = "prefab_create",
				.Title = "Create a prefab",
				.Description = "Saves an entity and its descendants as a prefab asset (.lprefab), to instance later",
				.InputSchema = Schema(R"({"type": "object", "properties": {
					"entity": {"type": "string", "description": "UUID of the prefab's root"},
					"path": {"type": "string", "description": "Relative to the project's Assets directory, ending in .lprefab"}},
					"required": ["entity", "path"]})"),
				.Handler = [&context](const Json::Value& json)
				{
					const Arguments arguments(json);
					const auto id = arguments.Id("entity");
					const auto path = arguments.String("path");
					if (!id)
						return Fail(id.error());
					if (!path)
						return Fail(path.error());
					if (auto saved = context.SavePrefab(*id, *path); !saved)
						return Fail(saved.error());
					const AssetInfo* asset = context.GetAssets()->FindByPath(PathFromUtf8(*path));
					Json::Value result = {{"path", *path}};
					result["asset"] = asset != nullptr ? IdOrNull(asset->Metadata.Id) : Json::Value();
					return McpToolResult::Structured(std::move(result));
				}});

			server.AddTool({.Name = "prefab_instantiate",
				.Title = "Instance a prefab",
				.Description = "Copies a prefab's entities into the open document, with new UUIDs, optionally under a "
							   "parent. Returns the instance's root UUID",
				.InputSchema = Schema(R"({"type": "object", "properties": {
					"path": {"type": "string", "description": "The prefab, relative to the project's Assets directory"},
					"parent": {"type": ["string", "null"], "description": "The parent's UUID; absent or null for a root entity"},
					"index": {"type": "integer", "minimum": 0}},
					"required": ["path"]})"),
				.Handler = [&context](const Json::Value& json)
				{
					const Arguments arguments(json);
					const auto path = arguments.String("path");
					const auto parent = arguments.OptionalId("parent");
					const auto index = arguments.OptionalIndex("index");
					if (!path)
						return Fail(path.error());
					if (!parent)
						return Fail(parent.error());
					if (!index)
						return Fail(index.error());
					const auto instance = context.InstantiatePrefab(*path, *parent, *index);
					if (!instance)
						return Fail(instance.error());
					return McpToolResult::Structured(
						{{"done", context.GetHistory().GetUndoName().value_or("")}, {"id", instance->ToString()}});
				}});
		}

		void AddHistoryTools(McpServer& server, EditorContext& context)
		{
			server.AddTool({.Name = "edit_undo",
				.Title = "Undo",
				.Description = "Undoes the last edit, whether it was made in the editor or through MCP",
				.InputSchema = Schema(R"({"type": "object", "properties": {}})"),
				.Handler = [&context](const Json::Value&)
				{
					const std::optional<std::string> name = context.GetHistory().GetUndoName();
					if (auto undone = context.Undo(); !undone)
						return Fail(undone.error());
					return McpToolResult::Structured({{"undone", *name}});
				}});

			server.AddTool({.Name = "edit_redo",
				.Title = "Redo",
				.Description = "Redoes the last undone edit",
				.InputSchema = Schema(R"({"type": "object", "properties": {}})"),
				.Handler = [&context](const Json::Value&)
				{
					const std::optional<std::string> name = context.GetHistory().GetRedoName();
					if (auto redone = context.Redo(); !redone)
						return Fail(redone.error());
					return McpToolResult::Structured({{"redone", *name}});
				}});

			server.AddTool({.Name = "edit_history",
				.Title = "Undo history",
				.Description = "The edits that can be undone, most recent first, and the next one to redo",
				.InputSchema = Schema(R"({"type": "object", "properties": {}})"),
				.ReadOnly = true,
				.Handler = [&context](const Json::Value&)
				{
					Json::Value undo = Json::Value::array();
					for (const std::string& name : context.GetHistory().GetUndoNames())
						undo.push_back(name);
					const std::optional<std::string> redo = context.GetHistory().GetRedoName();
					return McpToolResult::Structured(
						{{"undo", std::move(undo)}, {"redo", redo ? Json::Value(*redo) : Json::Value()}});
				}});
		}

		void AddPlayTools(McpServer& server, EditorContext& context)
		{
			const auto describe = [&context]
			{
				const Simulation* simulation = context.GetSimulation();
				return McpToolResult::Structured({{"state", ToString(context.GetPlayState())},
					{"tick", simulation != nullptr ? simulation->GetTick() : 0},
					{"queuedInput", context.GetQueuedInputCount()}});
			};

			server.AddTool({.Name = "play_start",
				.Title = "Start play mode",
				.Description =
					"Runs the simulation on the open document, in real time or paused. Stopping restores the "
					"document exactly as it was; edits are refused while playing",
				.InputSchema = Schema(R"({"type": "object", "properties": {
					"paused": {"type": "boolean", "description": "Start paused at tick 0, to step deterministically with play_step; false by default"}}})"),
				.Handler = [&context, describe](const Json::Value& json)
				{
					const auto paused = Arguments(json).Bool("paused", false);
					if (!paused)
						return Fail(paused.error());
					if (auto started = context.StartPlay(); !started)
						return Fail(started.error());
					// Before the main loop runs again, so no real time passes unpaused
					if (*paused)
					{
						const auto set = context.SetPaused(true);
						LS_CORE_ASSERT(set.has_value());
					}
					return describe();
				}});

			server.AddTool({.Name = "play_stop",
				.Title = "Stop play mode",
				.Description = "Stops the simulation and restores the document as it was before playing",
				.InputSchema = Schema(R"({"type": "object", "properties": {}})"),
				.Handler = [&context, describe](const Json::Value&)
				{
					if (auto stopped = context.StopPlay(); !stopped)
						return Fail(stopped.error());
					return describe();
				}});

			server.AddTool({.Name = "play_pause",
				.Title = "Pause or resume play mode",
				.Description = "Pauses the simulation, so it can be stepped a tick at a time, or resumes it",
				.InputSchema = Schema(
					R"({"type": "object", "properties": {"paused": {"type": "boolean", "description": "true by default"}}})"),
				.Handler = [&context, describe](const Json::Value& json)
				{
					const auto paused = Arguments(json).Bool("paused", true);
					if (!paused)
						return Fail(paused.error());
					if (auto set = context.SetPaused(*paused); !set)
						return Fail(set.error());
					return describe();
				}});

			server.AddTool({.Name = "play_step",
				.Title = "Step the simulation",
				.Description = "Runs simulation ticks right away, using queued input first (see input_send)",
				.InputSchema = Schema(
					R"({"type": "object", "properties": {"ticks": {"type": "integer", "minimum": 1, "maximum": 10000}}})"),
				.Handler = [&context, describe](const Json::Value& json)
				{
					const auto ticks = Arguments(json).UInt("ticks", 1, 1, MaxTicksPerCall);
					if (!ticks)
						return Fail(ticks.error());
					if (auto stepped = context.Step(*ticks); !stepped)
						return Fail(stepped.error());
					return describe();
				}});

			server.AddTool({.Name = "play_status",
				.Title = "Play mode status",
				.Description = "Whether play mode runs, the current tick and how many ticks of input are queued",
				.InputSchema = Schema(R"({"type": "object", "properties": {}})"),
				.ReadOnly = true,
				.Handler = [describe](const Json::Value&) { return describe(); }});

			server.AddTool({.Name = "input_send",
				.Title = "Send input",
				.Description =
					"Queues player 0's input for the coming ticks of play mode, to playtest: keys and mouse "
					"buttons held for the ticks (then released), and keys tapped (pressed and released) in the "
					"first one. Names are like \"W\", \"Space\", \"Left\", \"Escape\" for keys and \"Left\", "
					"\"Right\", \"Middle\" for mouse buttons",
				.InputSchema = Schema(R"({"type": "object", "properties": {
					"hold": {"type": "array", "items": {"type": "string"}, "description": "Keys held"},
					"tap": {"type": "array", "items": {"type": "string"}, "description": "Keys tapped in the first tick"},
					"mouseHold": {"type": "array", "items": {"type": "string"}, "description": "Mouse buttons held"},
					"mouseTap": {"type": "array", "items": {"type": "string"}, "description": "Mouse buttons clicked in the first tick"},
					"ticks": {"type": "integer", "minimum": 1, "maximum": 10000, "description": "How long, 1 by default"}}})"),
				.Handler = [&context, describe](const Json::Value& json)
				{
					if (context.GetPlayState() == PlayState::Editing)
						return McpToolResult::Failure("Start play mode first (play_start)");
					const Arguments arguments(json);
					const auto ticks = arguments.UInt("ticks", 1, 1, MaxTicksPerCall);
					if (!ticks)
						return Fail(ticks.error());

					InputCommand held;
					const auto readKeys = [&arguments](std::string_view key,
											  std::bitset<KeyCodeCount>& keys) -> std::expected<void, Error>
					{
						const auto names = arguments.Strings(key);
						if (!names)
							return std::unexpected(names.error());
						for (const std::string& name : *names)
						{
							const std::optional<Key> code = KeyFromName(name);
							if (!code)
								return std::unexpected(ArgumentError(fmt::format("'{}' isn't a key", name)));
							keys.set(std::to_underlying(*code));
						}
						return {};
					};
					const auto readButtons = [&arguments](std::string_view key,
												 std::bitset<MouseButtonCount>& buttons) -> std::expected<void, Error>
					{
						const auto names = arguments.Strings(key);
						if (!names)
							return std::unexpected(names.error());
						for (const std::string& name : *names)
						{
							const std::optional<MouseButton> button = MouseButtonFromName(name);
							if (!button)
								return std::unexpected(ArgumentError(fmt::format("'{}' isn't a mouse button", name)));
							buttons.set(std::to_underlying(*button));
						}
						return {};
					};
					std::bitset<KeyCodeCount> tapped;
					std::bitset<MouseButtonCount> clicked;
					if (auto read = readKeys("hold", held.KeysDown); !read)
						return Fail(read.error());
					if (auto read = readKeys("tap", tapped); !read)
						return Fail(read.error());
					if (auto read = readButtons("mouseHold", held.MouseButtonsDown); !read)
						return Fail(read.error());
					if (auto read = readButtons("mouseTap", clicked); !read)
						return Fail(read.error());

					// The first tick has the presses; held input stays down for every tick, and its release follows
					std::vector<InputCommand> commands(*ticks, held);
					commands.front().KeysPressed = held.KeysDown | tapped;
					commands.front().KeysReleased = tapped;
					commands.front().MouseButtonsPressed = held.MouseButtonsDown | clicked;
					commands.front().MouseButtonsReleased = clicked;
					if (held.KeysDown.any() || held.MouseButtonsDown.any())
					{
						InputCommand release;
						release.KeysReleased = held.KeysDown;
						release.MouseButtonsReleased = held.MouseButtonsDown;
						commands.push_back(release);
					}
					context.QueueInput(commands);
					return describe();
				}});
		}

		void AddViewTools(McpServer& server, EditorContext& context, ViewportCapture capture)
		{
			server.AddTool({.Name = "viewport_screenshot",
				.Title = "Screenshot the viewport",
				.Description = "Renders the open document from the editor camera and returns it as a PNG. Until the "
							   "scene renderer arrives, entities show as shaded cubes on a ground grid, the selection "
							   "in orange",
				.InputSchema = Schema(R"({"type": "object", "properties": {
					"width": {"type": "integer", "minimum": 16, "maximum": 4096, "description": "1280 by default"},
					"height": {"type": "integer", "minimum": 16, "maximum": 4096, "description": "720 by default"}}})"),
				.ReadOnly = true,
				.Handler = [capture = std::move(capture)](const Json::Value& json)
				{
					const Arguments arguments(json);
					const auto width = arguments.UInt("width", 1280, MinScreenshotSize, MaxScreenshotSize);
					const auto height = arguments.UInt("height", 720, MinScreenshotSize, MaxScreenshotSize);
					if (!width)
						return Fail(width.error());
					if (!height)
						return Fail(height.error());
					if (!capture)
						return McpToolResult::Failure(
							"Screenshots aren't available: the editor has no graphics device");
					const auto image = capture(*width, *height);
					if (!image)
						return Fail(image.error());
					const auto png = image->EncodePng();
					if (!png)
						return Fail(png.error());
					return McpToolResult::Image(
						EncodeBase64(*png), "image/png", fmt::format("The viewport, {}x{} pixels", *width, *height));
				}});

			server.AddTool({.Name = "camera_set",
				.Title = "Place the editor camera",
				.Description = "Places the viewport camera at a position, looking at a target",
				.InputSchema = Schema(R"({"type": "object", "properties": {
					"position": {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3},
					"target": {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3}},
					"required": ["position", "target"]})"),
				.Handler = [&context](const Json::Value& json)
				{
					const Arguments arguments(json);
					const auto position = arguments.Vec3("position");
					const auto target = arguments.Vec3("target");
					if (!position)
						return Fail(position.error());
					if (!target)
						return Fail(target.error());
					if (*position == *target)
						return McpToolResult::Failure("The camera's position and target must differ");
					context.GetCamera().LookAt(*position, *target);
					return McpToolResult::Text("Placed the camera");
				}});

			server.AddTool({.Name = "camera_frame",
				.Title = "Frame an entity",
				.Description = "Points the viewport camera at an entity's world position, keeping its angle",
				.InputSchema =
					Schema(R"({"type": "object", "properties": {"entity": {"type": "string", "description": "UUID"}},
					"required": ["entity"]})"),
				.Handler = [&context](const Json::Value& json)
				{
					const auto id = Arguments(json).Id("entity");
					if (!id)
						return Fail(id.error());
					const Entity entity = context.GetScene().FindEntity(*id);
					if (!entity)
						return McpToolResult::Failure(fmt::format("The scene has no entity {}", *id));
					EditorCamera& camera = context.GetCamera();
					const glm::vec3 target(context.GetScene().GetWorldMatrix(entity)[3]);
					const glm::vec3 offset = camera.GetPosition() - camera.GetTarget();
					camera.LookAt(target + offset, target);
					return McpToolResult::Text("Framed the entity");
				}});
		}

		void AddLogAndAssetTools(McpServer& server, EditorContext& context)
		{
			server.AddTool({.Name = "log_read",
				.Title = "Read the log",
				.Description = "Recent log messages, oldest first. Pass the latest sequence number you've seen as "
							   "'after' to get only newer ones",
				.InputSchema = Schema(R"({"type": "object", "properties": {
					"after": {"type": "integer", "minimum": 0},
					"level": {"type": "string", "enum": ["trace", "debug", "info", "warn", "error", "critical"]},
					"max": {"type": "integer", "minimum": 1, "maximum": 1000, "description": "100 by default"}}})"),
				.ReadOnly = true,
				.Handler = [&context](const Json::Value& json)
				{
					const Arguments arguments(json);
					const auto after = arguments.UInt("after", 0, 0, std::numeric_limits<uint32_t>::max());
					const auto max = arguments.UInt("max", 100, 1, MaxLogEntries);
					const auto levelName = arguments.OptionalString("level");
					if (!after)
						return Fail(after.error());
					if (!max)
						return Fail(max.error());
					if (!levelName)
						return Fail(levelName.error());
					LogLevel level = LogLevel::Trace;
					if (*levelName)
					{
						const auto parsed = ParseLogLevel(**levelName);
						if (!parsed)
							return Fail(parsed.error());
						level = *parsed;
					}
					Json::Value entries = Json::Value::array();
					for (const LogEntry& entry : context.GetLog().GetEntries(*after, level, *max))
					{
						entries.push_back({{"sequence", entry.Sequence}, {"level", ToLowerName(entry.Level)},
							{"logger", entry.Logger}, {"message", entry.Message}, {"time", entry.Time}});
					}
					return McpToolResult::Structured(
						{{"entries", std::move(entries)}, {"latest", context.GetLog().GetLatestSequence()}});
				}});

			server.AddTool({.Name = "asset_list",
				.Title = "List assets",
				.Description = "The project's assets: UUID, path and type",
				.InputSchema = Schema(R"({"type": "object", "properties": {
					"type": {"type": "string", "enum": ["Scene", "Prefab", "Texture", "Model", "Environment", "Audio", "Font", "Script"]}}})"),
				.ReadOnly = true,
				.Handler = [&context](const Json::Value& json)
				{
					if (context.GetAssets() == nullptr)
						return McpToolResult::Failure("No project is open");
					const auto typeName = Arguments(json).OptionalString("type");
					if (!typeName)
						return Fail(typeName.error());
					std::optional<AssetType> type;
					if (*typeName)
					{
						type = AssetTypeFromString(**typeName);
						if (!type)
							return McpToolResult::Failure(fmt::format("'{}' isn't an asset type", **typeName));
					}
					std::vector<const AssetInfo*> assets = context.GetAssets()->GetAll();
					std::erase_if(
						assets, [&type](const AssetInfo* asset) { return type && asset->Metadata.Type != *type; });
					Json::Value list = Json::Value::array();
					for (const AssetInfo* asset : assets)
					{
						list.push_back({{"id", asset->Metadata.Id.ToString()}, {"path", asset->Path},
							{"type", ToString(asset->Metadata.Type)}});
					}
					return McpToolResult::Structured({{"assets", std::move(list)}});
				}});

			server.AddTool({.Name = "asset_import",
				.Title = "Import an asset",
				.Description = "Copies a file into the project's assets and registers it. Returns its UUID",
				.InputSchema = Schema(R"({"type": "object", "properties": {
					"source": {"type": "string", "description": "The file to copy"},
					"path": {"type": "string", "description": "Where it goes, relative to the project's Assets directory"},
					"overwrite": {"type": "boolean", "description": "Replace a file already there"}},
					"required": ["source", "path"]})"),
				.Handler = [&context](const Json::Value& json)
				{
					const Arguments arguments(json);
					const auto source = arguments.String("source");
					const auto path = arguments.String("path");
					const auto overwrite = arguments.Bool("overwrite", false);
					if (!source)
						return Fail(source.error());
					if (!path)
						return Fail(path.error());
					if (!overwrite)
						return Fail(overwrite.error());
					const auto destination = context.ResolveAssetPath(*path);
					if (!destination)
						return Fail(destination.error());
					if (!GetAssetTypeForFile(*destination))
						return McpToolResult::Failure(
							fmt::format("{} doesn't have the extension of an asset type", *path));

					std::error_code error;
					std::filesystem::create_directories(destination->parent_path(), error);
					const auto options = *overwrite ? std::filesystem::copy_options::overwrite_existing
													: std::filesystem::copy_options::none;
					if (!error)
						std::filesystem::copy_file(PathFromUtf8(*source), *destination, options, error);
					if (error)
						return McpToolResult::Failure(
							fmt::format("Copying {} to {} failed: {}", *source, *path, error.message()));
					if (auto scanned = context.GetAssets()->Scan(); !scanned)
						return Fail(scanned.error());
					const AssetInfo* asset = context.GetAssets()->FindByPath(PathFromUtf8(*path));
					if (asset == nullptr)
						return McpToolResult::Failure(
							fmt::format("{} was copied but couldn't be registered (see the log)", *path));
					return McpToolResult::Structured({{"id", asset->Metadata.Id.ToString()}, {"path", asset->Path}});
				}});

			server.AddTool({.Name = "asset_rescan",
				.Title = "Rescan assets",
				.Description =
					"Finds new, moved and removed assets in the project's Assets directory, creating metadata "
					"for new ones",
				.InputSchema = Schema(R"({"type": "object", "properties": {}})"),
				.Handler = [&context](const Json::Value&)
				{
					if (context.GetAssets() == nullptr)
						return McpToolResult::Failure("No project is open");
					const auto report = context.GetAssets()->Scan();
					if (!report)
						return Fail(report.error());
					Json::Value errors = Json::Value::array();
					for (const Error& error : report->Errors)
						errors.push_back(error.GetMessageText());
					return McpToolResult::Structured({{"assetCount", context.GetAssets()->GetAssetCount()},
						{"created", report->CreatedMetadata}, {"reassigned", report->ReassignedIds},
						{"orphaned", report->OrphanedMetadata}, {"errors", std::move(errors)}});
				}});
		}

	}

	void RegisterEditorTools(McpServer& server, EditorContext& context, EditorToolHost host)
	{
		AddProjectTools(server, context, std::move(host.RequestQuit));
		AddDocumentTools(server, context);
		AddEntityTools(server, context);
		AddComponentTools(server, context);
		AddPrefabTools(server, context);
		AddHistoryTools(server, context);
		AddPlayTools(server, context);
		AddViewTools(server, context, std::move(host.Capture));
		AddLogAndAssetTools(server, context);
	}

}
