#include "Lodestone/Scene/SceneSerializer.h"

#include "Lodestone/Core/FileSystem.h"
#include "Lodestone/Scene/Entity.h"
#include "Lodestone/Serialization/Json.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/std.h>

#include <array>
#include <string>
#include <vector>

namespace Lodestone {

	namespace {

		constexpr const char* EntitiesKey = "entities";
		constexpr const char* ComponentsKey = "components";

		// Every entity, parents before their children, in hierarchy order
		std::vector<entt::entity> GetEntitiesInHierarchyOrder(const Scene& scene)
		{
			const entt::registry& registry = scene.GetRegistry();
			std::vector<entt::entity> ordered;
			ordered.reserve(scene.GetEntityCount());
			// Depth-first without recursion, so deep hierarchies can't overflow the stack. The stack holds entities
			// still to visit, in reverse, so they come off it in order
			std::vector<UUID> pending(scene.GetRootEntities().rbegin(), scene.GetRootEntities().rend());
			while (!pending.empty())
			{
				const entt::entity entity = scene.FindHandle(pending.back());
				pending.pop_back();
				ordered.push_back(entity);
				const std::vector<UUID>& children = registry.get<HierarchyComponent>(entity).Children;
				pending.insert(pending.end(), children.rbegin(), children.rend());
			}
			return ordered;
		}

		Json::Value SerializeComponent(const ComponentType& type, const void* component)
		{
			Json::Value fields = Json::Value::object();
			for (const FieldInfo& field : type.GetFields())
				fields[field.GetName()] = Json::FromFieldValue(field.Get(component));
			return fields;
		}

		std::expected<void, Error> DeserializeComponents(
			Scene& scene, Entity entity, const Json::Value& components, std::string_view context)
		{
			if (!components.is_object())
				return std::unexpected(Error(ErrorCode::ParseError,
					fmt::format("{}: expected an object, not {}", context, components.type_name())));

			entt::registry& registry = scene.GetRegistry();
			for (const auto& [name, fields] : components.items())
			{
				const std::string componentContext = fmt::format("{}.{}", context, name);
				const ComponentType* type = scene.GetComponentRegistry().Find(name);
				if (type == nullptr)
					return std::unexpected(Error(
						ErrorCode::ParseError, fmt::format("{}: there's no {} component", componentContext, name)));
				if (type->IsInternal())
					return std::unexpected(Error(ErrorCode::ParseError,
						fmt::format(
							"{}: {} is an internal component, which files can't contain", componentContext, name)));
				if (!fields.is_object())
					return std::unexpected(Error(ErrorCode::ParseError,
						fmt::format("{}: expected an object, not {}", componentContext, fields.type_name())));

				void* component = type->TryGet(registry, entity.GetHandle());
				if (component == nullptr)
					component = type->Add(registry, entity.GetHandle());
				for (const auto& [fieldName, json] : fields.items())
				{
					const std::string fieldContext = fmt::format("{}.{}", componentContext, fieldName);
					const FieldInfo* field = type->FindField(fieldName);
					if (field == nullptr)
						return std::unexpected(Error(ErrorCode::ParseError,
							fmt::format("{}: {} has no field '{}'", fieldContext, name, fieldName)));
					const auto value = Json::ToFieldValue(json, field->GetType(), fieldContext);
					if (!value)
						return std::unexpected(value.error());
					if (auto set = field->Set(component, *value); !set)
						return std::unexpected(set.error().WithContext(fieldContext));
				}
			}
			return {};
		}

		std::expected<Scope<Scene>, Error> DeserializeScene(
			const Json::Value& document, const ComponentRegistry& components)
		{
			constexpr std::array<std::string_view, 3> documentMembers = {"format", "version", EntitiesKey};
			if (auto checked = Json::CheckMembers(document, documentMembers, "The scene"); !checked)
				return std::unexpected(checked.error());
			const auto entities = Json::GetMember(document, EntitiesKey, "The scene");
			if (!entities)
				return std::unexpected(entities.error());
			if (!(*entities)->is_array())
				return std::unexpected(Error(ErrorCode::ParseError,
					fmt::format("The scene's entities: expected an array, not {}", (*entities)->type_name())));

			auto scene = CreateScope<Scene>(components);

			// First every entity, so components can refer to entities that come later in the file
			const Json::Value& entityList = **entities;
			std::vector<Entity> created;
			std::vector<const Json::Value*> componentLists;
			created.reserve(entityList.size());
			componentLists.reserve(entityList.size());
			for (size_t index = 0; index < entityList.size(); ++index)
			{
				const std::string context = fmt::format("entities[{}]", index);
				constexpr std::array<std::string_view, 1> entityMembers = {ComponentsKey};
				if (auto checked = Json::CheckMembers(entityList[index], entityMembers, context); !checked)
					return std::unexpected(checked.error());
				const auto componentsJson = Json::GetMember(entityList[index], ComponentsKey, context);
				if (!componentsJson)
					return std::unexpected(componentsJson.error());
				const auto idComponent = Json::GetMember(**componentsJson, "ID", fmt::format("{}.components", context));
				if (!idComponent)
					return std::unexpected(idComponent.error());
				const auto id = Json::GetUUID(**idComponent, "ID", fmt::format("{}.components.ID", context));
				if (!id)
					return std::unexpected(id.error());
				auto entity = scene->CreateEntityWithId(*id);
				if (!entity)
					return std::unexpected(entity.error().WithContext(context));
				created.push_back(*entity);
				componentLists.push_back(*componentsJson);
			}

			// Then their components. Parents are applied last, all at once, which keeps the hierarchy consistent and
			// rejects cycles; children keep the order they're listed in
			std::vector<UUID> parents;
			parents.reserve(created.size());
			for (size_t index = 0; index < created.size(); ++index)
			{
				const std::string context = fmt::format("entities[{}].components", index);
				Entity entity = created[index];
				if (auto loaded = DeserializeComponents(*scene, entity, *componentLists[index], context); !loaded)
					return std::unexpected(loaded.error());
				auto& hierarchy = entity.Get<HierarchyComponent>();
				parents.push_back(hierarchy.Parent);
				hierarchy.Parent = UUID();
			}
			for (size_t index = 0; index < parents.size(); ++index)
			{
				if (!parents[index].IsNil() && !scene->Contains(parents[index]))
					return std::unexpected(Error(ErrorCode::ParseError,
						fmt::format("entities[{}].components.Hierarchy.Parent: the scene has no entity {}", index,
							parents[index])));
			}
			if (auto built = scene->BuildHierarchy(created, parents); !built)
				return std::unexpected(Error(ErrorCode::ParseError, built.error().GetMessageText()));
			return scene;
		}

	}

	const FileFormat& SceneSerializer::GetFormat()
	{
		static const FileFormat Format{.Name = "Lodestone.Scene", .CurrentVersion = 1, .Migrations = {}};
		return Format;
	}

	Json::Value SceneSerializer::Serialize(const Scene& scene)
	{
		const entt::registry& registry = scene.GetRegistry();
		Json::Value entities = Json::Value::array();
		for (const entt::entity entity : GetEntitiesInHierarchyOrder(scene))
		{
			Json::Value components = Json::Value::object();
			for (const ComponentType* type : scene.GetComponentRegistry().GetTypes())
			{
				if (type->IsInternal())
					continue;
				if (const void* component = type->TryGet(registry, entity))
					components[type->GetName()] = SerializeComponent(*type, component);
			}
			entities.push_back({{ComponentsKey, std::move(components)}});
		}

		Json::Value document = CreateDocument(GetFormat());
		document[EntitiesKey] = std::move(entities);
		return document;
	}

	std::expected<Scope<Scene>, Error> SceneSerializer::Deserialize(
		Json::Value document, const ComponentRegistry& components)
	{
		if (auto upgraded = UpgradeDocument(document, GetFormat()); !upgraded)
			return std::unexpected(upgraded.error());
		// The checks above leave nothing for nlohmann/json to throw, but the document comes from outside
		try
		{
			return DeserializeScene(document, components);
		}
		catch (const Json::Value::exception& exception)
		{
			return std::unexpected(Error(ErrorCode::ParseError, exception.what()));
		}
	}

	std::string SceneSerializer::SerializeToText(const Scene& scene)
	{
		return Json::Write(Serialize(scene));
	}

	std::expected<Scope<Scene>, Error> SceneSerializer::DeserializeFromText(
		std::string_view text, const ComponentRegistry& components)
	{
		auto document = Json::Parse(text);
		if (!document)
			return std::unexpected(document.error());
		return Deserialize(std::move(*document), components);
	}

	std::expected<void, Error> SceneSerializer::Save(const Scene& scene, const std::filesystem::path& path)
	{
		return WriteFileAtomically(path, SerializeToText(scene))
			.transform_error(
				[&path](const Error& error) { return error.WithContext(fmt::format("Saving scene {}", path)); });
	}

	std::expected<Scope<Scene>, Error> SceneSerializer::Load(
		const std::filesystem::path& path, const ComponentRegistry& components)
	{
		const auto text = ReadFile(path);
		if (!text)
			return std::unexpected(text.error().WithContext(fmt::format("Loading scene {}", path)));
		return DeserializeFromText(*text, components)
			.transform_error(
				[&path](const Error& error) { return error.WithContext(fmt::format("Loading scene {}", path)); });
	}

}
