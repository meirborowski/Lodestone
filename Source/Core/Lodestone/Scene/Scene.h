#pragma once

#include "Lodestone/Core/Error.h"
#include "Lodestone/Core/RandomGenerator.h"
#include "Lodestone/Core/UUID.h"
#include "Lodestone/Reflection/ComponentRegistry.h"
#include "Lodestone/Reflection/SnapshotArchive.h"
#include "Lodestone/Scene/Components.h"

#include <entt/entity/registry.hpp>
#include <glm/mat4x4.hpp>

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Lodestone {

	class Entity;

	// Everything a scene holds, copied for simulation rollback (see Scene::SaveSnapshot)
	struct SceneSnapshot
	{
		SnapshotArchive Registry;
		std::vector<UUID> Roots;
		RandomGenerator IdGenerator;
	};

	// A set of entities and their components, arranged in a hierarchy. Every entity has the required components (ID,
	// name, transform and hierarchy), and a UUID that's unique within the scene.
	//
	// New entities get their UUIDs from the scene's own random generator, seeded randomly when the scene is created
	// and saved in its snapshots - so re-simulating ticks after restoring a snapshot creates entities with the same
	// UUIDs as before.
	//
	// Scenes are used from one thread at a time
	class Scene
	{
	public:
		explicit Scene(const ComponentRegistry& components = GetEngineComponentRegistry());
		~Scene() = default;

		Scene(const Scene&) = delete;
		Scene& operator=(const Scene&) = delete;
		// Entities point to their scene, so it never moves
		Scene(Scene&&) = delete;
		Scene& operator=(Scene&&) = delete;

		// Creates an entity with a new UUID and the required components, as the last root entity
		Entity CreateEntity(std::string_view name = {});
		// A UUID from the scene's generator that no entity in the scene has
		UUID GenerateEntityId();
		// Creates an entity with the given UUID, which mustn't be nil or used by another entity in the scene
		[[nodiscard]] std::expected<Entity, Error> CreateEntityWithId(UUID id, std::string_view name = {});
		// Destroys an entity and all its descendants
		void DestroyEntity(Entity entity);

		// The entity with this UUID, or an invalid entity if there's none
		Entity FindEntity(UUID id);
		// The handle of the entity with this UUID, or entt::null if there's none
		entt::entity FindHandle(UUID id) const;
		bool Contains(UUID id) const { return m_Entities.contains(id); }
		size_t GetEntityCount() const { return m_Entities.size(); }

		// Moves an entity under a new parent, or to the root with an invalid parent, at a position among its new
		// siblings (by default, the end). The local transform is kept. Fails if the parent is the entity itself or one
		// of its descendants
		[[nodiscard]] std::expected<void, Error> SetParent(
			Entity entity, Entity parent, std::optional<size_t> index = std::nullopt);
		// Arranges many entities at once, for loaders: parents[i] is the parent of entities[i] (nil for a root), and
		// siblings end up in the order they're given in. The entities must be roots without children. Takes time in
		// proportion to the number of entities however deep the hierarchy is, so a hostile file can't stall it. Fails,
		// changing nothing, if a parent isn't in the scene or the parents form a cycle
		[[nodiscard]] std::expected<void, Error> BuildHierarchy(
			std::span<const Entity> entities, std::span<const UUID> parents);
		// The root entities, in order
		std::span<const UUID> GetRootEntities() const { return m_Roots; }
		// The entity's transform relative to the world: its local transform combined with its ancestors'
		glm::mat4 GetWorldMatrix(Entity entity) const;

		entt::registry& GetRegistry() { return m_Registry; }
		const entt::registry& GetRegistry() const { return m_Registry; }
		const ComponentRegistry& GetComponentRegistry() const { return *m_Components; }

		// Copies the whole scene - every entity and every registered component, including internal ones - so it can
		// be restored exactly, entity handles included. Components that aren't registered aren't copied
		[[nodiscard]] SceneSnapshot SaveSnapshot() const;
		// Replaces the scene's contents with a snapshot from SaveSnapshot(). Entities keep the handles they had when
		// the snapshot was taken, and entities created afterwards get the same handles as they did originally
		void RestoreSnapshot(const SceneSnapshot& snapshot);

	private:
		// The handle of an entity that the hierarchy refers to, which must exist
		entt::entity GetHandle(UUID id) const;
		// The root list, or the children of the entity's parent
		std::vector<UUID>& GetSiblings(UUID parent);
		void CollectDescendants(entt::entity entity, std::vector<entt::entity>& descendants) const;

	private:
		const ComponentRegistry* m_Components;
		entt::registry m_Registry;
		std::unordered_map<UUID, entt::entity> m_Entities;
		std::vector<UUID> m_Roots;
		RandomGenerator m_IdGenerator;
	};

}
