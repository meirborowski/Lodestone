#include "Lodestone/Scene/EntitySerializer.h"

#include <spdlog/fmt/fmt.h>

#include <array>
#include <cstddef>
#include <string>
#include <unordered_map>

namespace Lodestone {

	namespace {

		constexpr const char* ComponentsKey = "components";

		// Entities and their descendants, parents before their children, in hierarchy order
		std::vector<entt::entity> GetEntitiesInHierarchyOrder(const Scene& scene, std::span<const UUID> roots)
		{
			const entt::registry& registry = scene.GetRegistry();
			std::vector<entt::entity> ordered;
			// Depth-first without recursion, so deep hierarchies can't overflow the stack. The stack holds entities
			// still to visit, in reverse, so they come off it in order
			std::vector<UUID> pending(roots.rbegin(), roots.rend());
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

		Json::Value SerializeEntity(const Scene& scene, entt::entity entity)
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
			Json::Value serialized = Json::Value::object();
			serialized[ComponentsKey] = std::move(components);
			return serialized;
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

		std::expected<std::vector<Entity>, Error> CreateEntities(
			Scene& scene, const Json::Value& entities, std::string_view context)
		{
			if (!entities.is_array())
				return std::unexpected(Error(ErrorCode::ParseError,
					fmt::format("{}: expected an array, not {}", context, entities.type_name())));

			// First every entity, so components can refer to entities that come later in the list
			std::vector<Entity> created;
			std::vector<const Json::Value*> componentLists;
			created.reserve(entities.size());
			componentLists.reserve(entities.size());
			for (size_t index = 0; index < entities.size(); ++index)
			{
				const std::string entityContext = fmt::format("{}[{}]", context, index);
				constexpr std::array<std::string_view, 1> entityMembers = {ComponentsKey};
				if (auto checked = Json::CheckMembers(entities[index], entityMembers, entityContext); !checked)
					return std::unexpected(checked.error());
				const auto components = Json::GetMember(entities[index], ComponentsKey, entityContext);
				if (!components)
					return std::unexpected(components.error());
				const auto idComponent =
					Json::GetMember(**components, "ID", fmt::format("{}.components", entityContext));
				if (!idComponent)
					return std::unexpected(idComponent.error());
				const auto id = Json::GetUUID(**idComponent, "ID", fmt::format("{}.components.ID", entityContext));
				if (!id)
					return std::unexpected(id.error());
				auto entity = scene.CreateEntityWithId(*id);
				if (!entity)
					return std::unexpected(entity.error().WithContext(entityContext));
				created.push_back(*entity);
				componentLists.push_back(*components);
			}

			// Then their components. Parents are applied last, all at once, which keeps the hierarchy consistent and
			// rejects cycles; children keep the order they're listed in
			std::vector<UUID> parents;
			parents.reserve(created.size());
			for (size_t index = 0; index < created.size(); ++index)
			{
				const std::string componentsContext = fmt::format("{}[{}].components", context, index);
				Entity entity = created[index];
				const auto loaded = DeserializeComponents(scene, entity, *componentLists[index], componentsContext);
				// The parent from the file waits until the hierarchy is built. Until then every entity is a root, as
				// its Hierarchy component must say - even when loading fails, so the entities can be destroyed again
				auto& hierarchy = entity.Get<HierarchyComponent>();
				parents.push_back(hierarchy.Parent);
				hierarchy.Parent = UUID();
				if (!loaded)
					return std::unexpected(loaded.error());
			}
			for (size_t index = 0; index < parents.size(); ++index)
			{
				if (!parents[index].IsNil() && !scene.Contains(parents[index]))
					return std::unexpected(Error(ErrorCode::ParseError,
						fmt::format("{}[{}].components.Hierarchy.Parent: the scene has no entity {}", context, index,
							parents[index])));
			}
			if (auto built = scene.BuildHierarchy(created, parents); !built)
				return std::unexpected(Error(ErrorCode::ParseError, built.error().GetMessageText()));
			return created;
		}

	}

	Json::Value EntitySerializer::SerializeScene(const Scene& scene)
	{
		Json::Value entities = Json::Value::array();
		for (const entt::entity entity : GetEntitiesInHierarchyOrder(scene, scene.GetRootEntities()))
			entities.push_back(SerializeEntity(scene, entity));
		return entities;
	}

	Json::Value EntitySerializer::SerializeTree(const Scene& scene, entt::entity root)
	{
		const std::array rootId = {scene.GetRegistry().get<IDComponent>(root).ID};
		Json::Value entities = Json::Value::array();
		for (const entt::entity entity : GetEntitiesInHierarchyOrder(scene, rootId))
			entities.push_back(SerializeEntity(scene, entity));
		// The tree stands on its own: wherever it's instantiated, its root goes under a parent of that scene's choosing
		entities[0][ComponentsKey]["Hierarchy"]["Parent"] = UUID().ToString();
		return entities;
	}

	std::expected<std::vector<Entity>, Error> EntitySerializer::DeserializeEntities(
		Scene& scene, const Json::Value& entities, std::string_view context)
	{
		// Entities are created before everything about them is checked, so on failure they're destroyed again. Until
		// the hierarchy is built - the last step, which changes nothing when it fails - they're the last root entities
		const size_t rootCountBefore = scene.GetRootEntities().size();
		auto created = CreateEntities(scene, entities, context);
		if (!created)
		{
			const std::vector<UUID> leftOver(
				scene.GetRootEntities().begin() + static_cast<std::ptrdiff_t>(rootCountBefore),
				scene.GetRootEntities().end());
			for (const UUID root : leftOver)
				scene.DestroyEntity(scene.FindEntity(root));
			return std::unexpected(created.error());
		}
		return created;
	}

	std::expected<Entity, Error> EntitySerializer::InstantiateTree(
		Scene& scene, const Json::Value& entities, IdPolicy ids, Entity parent, std::optional<size_t> index)
	{
		if (parent && (parent.GetScene() != &scene || !parent.IsValid()))
			return std::unexpected(Error(ErrorCode::InvalidArgument, "The parent isn't in the scene"));

		// Read the tree into a scene of its own first, so nothing in the target changes unless all of it is valid
		Scene source(scene.GetComponentRegistry());
		const auto read = DeserializeEntities(source, entities, "entities");
		if (!read)
			return std::unexpected(read.error());
		if (read->empty() || source.GetRootEntities().size() != 1 || source.GetRootEntities()[0] != (*read)[0].GetId())
			return std::unexpected(
				Error(ErrorCode::InvalidArgument, "The entities aren't a single tree with its root listed first"));

		std::unordered_map<UUID, UUID> idMap;
		for (const Entity& entity : *read)
		{
			const UUID id = entity.GetId();
			if (ids == IdPolicy::Keep && scene.Contains(id))
				return std::unexpected(
					Error(ErrorCode::AlreadyExists, fmt::format("The scene already has an entity with UUID {}", id)));
			idMap.emplace(id, ids == IdPolicy::Keep ? id : scene.GenerateEntityId());
		}

		const auto mapId = [&idMap](UUID id)
		{
			const auto mapped = idMap.find(id);
			LS_CORE_ASSERT(mapped != idMap.end(), "Entity {} isn't in the tree", id);
			return mapped->second;
		};

		// Nothing can fail from here on
		std::vector<Entity> created;
		std::vector<UUID> parents;
		created.reserve(read->size());
		parents.reserve(read->size());
		entt::registry& target = scene.GetRegistry();
		for (const Entity& original : *read)
		{
			auto copy = scene.CreateEntityWithId(mapId(original.GetId()));
			LS_CORE_ASSERT(copy.has_value());
			for (const ComponentType* type : scene.GetComponentRegistry().GetTypes())
			{
				const void* component = type->TryGet(source.GetRegistry(), original.GetHandle());
				if (component == nullptr || type->IsInternal() ||
					type->GetTypeId() == entt::type_hash<IDComponent>::value() ||
					type->GetTypeId() == entt::type_hash<HierarchyComponent>::value())
					continue;
				void* destination = type->TryGet(target, copy->GetHandle());
				if (destination == nullptr)
					destination = type->Add(target, copy->GetHandle());
				for (const FieldInfo& field : type->GetFields())
				{
					FieldValue value = field.Get(component);
					// References between the tree's entities follow them to their new UUIDs
					if (const auto* reference = std::get_if<UUID>(&value))
					{
						if (const auto mapped = idMap.find(*reference); mapped != idMap.end())
							value = mapped->second;
					}
					const auto set = field.Set(destination, value);
					LS_CORE_ASSERT(set.has_value());
				}
				type->NotifyChanged(target, copy->GetHandle());
			}
			const UUID originalParent = original.Get<HierarchyComponent>().Parent;
			parents.push_back(originalParent.IsNil() ? UUID() : mapId(originalParent));
			created.push_back(*copy);
		}
		const auto built = scene.BuildHierarchy(created, parents);
		LS_CORE_ASSERT(built.has_value());

		Entity root = created.front();
		if (parent || index)
		{
			const auto placed = scene.SetParent(root, parent, index);
			LS_CORE_ASSERT(placed.has_value());
		}
		return root;
	}

}
