#pragma once

#include "Lodestone/Core/Error.h"
#include "Lodestone/Scene/Entity.h"
#include "Lodestone/Scene/Scene.h"
#include "Lodestone/Serialization/Json.h"

#include <cstddef>
#include <expected>
#include <optional>
#include <string_view>
#include <vector>

namespace Lodestone {

	// Converts entities to and from entity lists: the JSON that scene and prefab files hold, one object per entity with
	// every non-internal component, parents before their children:
	//
	//   [{"components": {"ID": {"ID": "..."}, "Name": {"Name": "Player"}, "Transform": {...}, ...}}, ...]
	//
	// Scenes, prefabs, duplicating, and undoing a delete all use it. Reading is strict and never leaves partly created
	// entities behind (see docs/Decisions/0011-file-formats.md)
	class EntitySerializer
	{
	public:
		// What happens to the UUIDs of entities copied into a scene
		enum class IdPolicy
		{
			// They keep their UUIDs, which mustn't be in use in the scene - undoing a delete
			Keep,
			// They get new UUIDs, and UUID fields that refer to entities among them are updated to match - so a copy's
			// entities refer to each other, not to the original's
			Regenerate,
		};

		// Every entity in the scene, in hierarchy order
		static Json::Value SerializeScene(const Scene& scene);
		// An entity and its descendants, in hierarchy order. The root is saved as a root entity: it doesn't refer to
		// its parent
		static Json::Value SerializeTree(const Scene& scene, entt::entity root);

		// Creates every entity of an entity list in a scene that has none of their UUIDs, with their hierarchy, and
		// returns them in list order. Parents must be in the list or already in the scene. context names the list in
		// error messages
		[[nodiscard]] static std::expected<std::vector<Entity>, Error> DeserializeEntities(
			Scene& scene, const Json::Value& entities, std::string_view context);

		// Copies a tree - an entity list from SerializeTree() - into a scene, with its root under parent (or a root
		// entity, if parent is invalid) at index among its siblings (by default, the end). Returns the root
		[[nodiscard]] static std::expected<Entity, Error> InstantiateTree(Scene& scene, const Json::Value& entities,
			IdPolicy ids, Entity parent = {}, std::optional<size_t> index = std::nullopt);
	};

}
