#include "Lodestone/Scene/Scene.h"

#include "Lodestone/Scene/Entity.h"

#include <entt/entity/snapshot.hpp>
#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Lodestone {

	namespace {

		// Through erase_if: MSVC's std::erase is vectorized for trivially comparable types, and its vectorized path
		// rejects 16-byte elements such as UUID when Clang (clang-cl and clang-tidy) compiles it
		void RemoveSibling(std::vector<UUID>& siblings, UUID id)
		{
			std::erase_if(siblings, [id](UUID sibling) { return sibling == id; });
		}

	}

	Scene::Scene(const ComponentRegistry& components)
		: m_Components(&components)
	{
	}

	Entity Scene::CreateEntity(std::string_view name)
	{
		auto entity = CreateEntityWithId(GenerateEntityId(), name);
		LS_CORE_ASSERT(entity.has_value());
		return *entity;
	}

	UUID Scene::GenerateEntityId()
	{
		// A collision between random UUIDs is practically impossible, but cheap to rule out
		UUID id;
		do
		{
			const uint64_t high = m_IdGenerator.Next();
			const uint64_t low = m_IdGenerator.Next();
			id = UUID::FromRandomBits(high, low);
		} while (Contains(id));
		return id;
	}

	std::expected<Entity, Error> Scene::CreateEntityWithId(UUID id, std::string_view name)
	{
		if (id.IsNil())
			return std::unexpected(Error(ErrorCode::InvalidArgument, "An entity can't have the nil UUID"));
		if (Contains(id))
			return std::unexpected(
				Error(ErrorCode::AlreadyExists, fmt::format("The scene already has an entity with UUID {}", id)));

		const entt::entity handle = m_Registry.create();
		m_Registry.emplace<IDComponent>(handle, id);
		m_Registry.emplace<NameComponent>(handle, std::string(name));
		m_Registry.emplace<TransformComponent>(handle);
		m_Registry.emplace<HierarchyComponent>(handle);
		// Required components that engine modules registered, beyond the core ones
		for (const ComponentType* type : m_Components->GetTypes())
		{
			if (type->IsRequired() && !type->Has(m_Registry, handle))
				type->Add(m_Registry, handle);
		}

		m_Entities.emplace(id, handle);
		m_Roots.push_back(id);
		return Entity(handle, this);
	}

	void Scene::DestroyEntity(Entity entity)
	{
		LS_CORE_ASSERT(entity.GetScene() == this && entity.IsValid(), "Destroying an entity that isn't in the scene");

		const auto& hierarchy = entity.Get<HierarchyComponent>();
		RemoveSibling(GetSiblings(hierarchy.Parent), entity.GetId());

		std::vector<entt::entity> doomed{entity.GetHandle()};
		CollectDescendants(entity.GetHandle(), doomed);
		for (const entt::entity handle : doomed)
		{
			m_Entities.erase(m_Registry.get<IDComponent>(handle).ID);
			m_Registry.destroy(handle);
		}
	}

	Entity Scene::FindEntity(UUID id)
	{
		const entt::entity handle = FindHandle(id);
		return handle != entt::null ? Entity(handle, this) : Entity();
	}

	entt::entity Scene::FindHandle(UUID id) const
	{
		const auto found = m_Entities.find(id);
		return found != m_Entities.end() ? found->second : entt::null;
	}

	std::expected<void, Error> Scene::SetParent(Entity entity, Entity parent, std::optional<size_t> index)
	{
		if (entity.GetScene() != this || !entity.IsValid())
			return std::unexpected(Error(ErrorCode::InvalidArgument, "The entity isn't in the scene"));
		if (parent && parent.GetScene() != this)
			return std::unexpected(Error(ErrorCode::InvalidArgument, "The parent isn't in the scene"));

		const UUID id = entity.GetId();
		const UUID newParent = parent ? parent.GetId() : UUID();
		// Walking up from the new parent must not reach the entity, or the hierarchy would contain a cycle
		for (UUID ancestor = newParent; !ancestor.IsNil();
			ancestor = m_Registry.get<HierarchyComponent>(GetHandle(ancestor)).Parent)
		{
			if (ancestor == id)
				return std::unexpected(Error(ErrorCode::InvalidArgument,
					fmt::format("Entity {} can't be the child of itself or of one of its descendants", id)));
		}

		auto& hierarchy = entity.Get<HierarchyComponent>();
		RemoveSibling(GetSiblings(hierarchy.Parent), id);
		std::vector<UUID>& siblings = GetSiblings(newParent);
		const size_t position = std::min(index.value_or(siblings.size()), siblings.size());
		siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(position), id);
		hierarchy.Parent = newParent;

		m_Registry.patch<HierarchyComponent>(entity.GetHandle());
		if (parent)
			m_Registry.patch<HierarchyComponent>(parent.GetHandle());
		return {};
	}

	std::expected<void, Error> Scene::BuildHierarchy(std::span<const Entity> entities, std::span<const UUID> parents)
	{
		LS_CORE_ASSERT(entities.size() == parents.size(), "Every entity needs a parent, or nil");

		std::unordered_map<UUID, size_t> indices;
		indices.reserve(entities.size());
		for (size_t index = 0; index < entities.size(); ++index)
		{
			const Entity& entity = entities[index];
			LS_CORE_ASSERT(entity.GetScene() == this && entity.IsValid(), "The entity isn't in the scene");
			LS_CORE_ASSERT(
				entity.Get<HierarchyComponent>().Parent.IsNil() && entity.Get<HierarchyComponent>().Children.empty(),
				"BuildHierarchy() takes root entities without children");
			indices.emplace(entity.GetId(), index);
		}
		for (const UUID parent : parents)
		{
			if (!parent.IsNil() && !Contains(parent))
				return std::unexpected(Error(
					ErrorCode::InvalidArgument, fmt::format("The scene has no entity {} to be a parent", parent)));
		}

		// A cycle can only run through the given entities: the rest of the scene is already a valid hierarchy. Each
		// entity is visited once - walks stop at entities already known to be fine
		enum class Visit : uint8_t
		{
			NotYet,
			OnPath,
			Done,
		};
		std::vector<Visit> visits(entities.size(), Visit::NotYet);
		std::vector<size_t> path;
		for (size_t start = 0; start < entities.size(); ++start)
		{
			for (size_t current = start; visits[current] == Visit::NotYet;)
			{
				visits[current] = Visit::OnPath;
				path.push_back(current);
				const auto next = indices.find(parents[current]);
				if (next == indices.end())
					break;
				if (visits[next->second] == Visit::OnPath)
					return std::unexpected(Error(ErrorCode::InvalidArgument,
						fmt::format("Entity {} would be its own ancestor: the hierarchy has a cycle",
							entities[next->second].GetId())));
				current = next->second;
			}
			for (const size_t visited : path)
				visits[visited] = Visit::Done;
			path.clear();
		}

		// Nothing can fail from here on
		std::unordered_set<UUID> moved;
		for (size_t index = 0; index < entities.size(); ++index)
		{
			if (parents[index].IsNil())
				continue;
			const UUID id = entities[index].GetId();
			const entt::entity parent = GetHandle(parents[index]);
			m_Registry.get<HierarchyComponent>(parent).Children.push_back(id);
			m_Registry.get<HierarchyComponent>(entities[index].GetHandle()).Parent = parents[index];
			m_Registry.patch<HierarchyComponent>(entities[index].GetHandle());
			m_Registry.patch<HierarchyComponent>(parent);
			moved.insert(id);
		}
		std::erase_if(m_Roots, [&moved](UUID root) { return moved.contains(root); });
		return {};
	}

	glm::mat4 Scene::GetWorldMatrix(Entity entity) const
	{
		LS_CORE_ASSERT(entity.GetScene() == this && entity.IsValid(), "The entity isn't in the scene");

		glm::mat4 world = m_Registry.get<TransformComponent>(entity.GetHandle()).GetMatrix();
		UUID parent = m_Registry.get<HierarchyComponent>(entity.GetHandle()).Parent;
		while (!parent.IsNil())
		{
			const entt::entity handle = GetHandle(parent);
			world = m_Registry.get<TransformComponent>(handle).GetMatrix() * world;
			parent = m_Registry.get<HierarchyComponent>(handle).Parent;
		}
		return world;
	}

	SceneSnapshot Scene::SaveSnapshot() const
	{
		SceneSnapshot snapshot;
		SnapshotArchive::Writer writer(snapshot.Registry);
		const entt::snapshot registrySnapshot(m_Registry);
		registrySnapshot.get<entt::entity>(writer);
		for (const ComponentType* type : m_Components->GetTypes())
			type->SaveSnapshot(registrySnapshot, writer);
		snapshot.Roots = m_Roots;
		snapshot.IdGenerator = m_IdGenerator;
		return snapshot;
	}

	void Scene::RestoreSnapshot(const SceneSnapshot& snapshot)
	{
		// A snapshot is restored into an empty registry, which recreates the same entity handles
		m_Registry = entt::registry();
		SnapshotArchive::Reader reader(snapshot.Registry);
		entt::snapshot_loader loader(m_Registry);
		loader.get<entt::entity>(reader);
		for (const ComponentType* type : m_Components->GetTypes())
			type->LoadSnapshot(loader, reader);
		LS_CORE_ASSERT(reader.IsAtEnd(), "The snapshot was taken with different component types");

		m_Entities.clear();
		for (const auto [handle, id] : m_Registry.view<const IDComponent>().each())
			m_Entities.emplace(id.ID, handle);
		m_Roots = snapshot.Roots;
		m_IdGenerator = snapshot.IdGenerator;
	}

	entt::entity Scene::GetHandle(UUID id) const
	{
		const auto found = m_Entities.find(id);
		LS_CORE_ASSERT(found != m_Entities.end(), "No entity has UUID {}", id);
		return found->second;
	}

	std::vector<UUID>& Scene::GetSiblings(UUID parent)
	{
		if (parent.IsNil())
			return m_Roots;
		return m_Registry.get<HierarchyComponent>(GetHandle(parent)).Children;
	}

	void Scene::CollectDescendants(entt::entity entity, std::vector<entt::entity>& descendants) const
	{
		// Breadth-first, without recursion, so deep hierarchies can't overflow the stack
		size_t next = descendants.size();
		for (const UUID child : m_Registry.get<HierarchyComponent>(entity).Children)
			descendants.push_back(GetHandle(child));
		while (next < descendants.size())
		{
			const entt::entity current = descendants[next++];
			for (const UUID child : m_Registry.get<HierarchyComponent>(current).Children)
				descendants.push_back(GetHandle(child));
		}
	}

}
