#pragma once

#include "Lodestone/Core/Error.h"
#include "Lodestone/Core/UUID.h"
#include "Lodestone/Scene/Entity.h"
#include "Lodestone/Scene/Scene.h"
#include "Lodestone/Serialization/FileFormat.h"
#include "Lodestone/Serialization/Json.h"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Lodestone {

	// Saves, loads and instances prefabs (.lprefab files): an entity and its descendants, saved to be copied into
	// scenes. The document holds an entity list (see EntitySerializer), root first:
	//
	//   {"format": "Lodestone.Prefab", "version": 1, "entities": [{"components": {...}}, ...]}
	//
	// Instances are independent copies with new UUIDs; their root's PrefabInstance component records the prefab asset
	// they came from (see docs/Decisions/0014-prefabs.md)
	class PrefabSerializer
	{
	public:
		static const FileFormat& GetFormat();

		static Json::Value Serialize(const Scene& scene, Entity root);
		static std::string SerializeToText(const Scene& scene, Entity root);
		// The prefab's entity list, checked as strictly as a scene: everything in it is known to instance cleanly
		[[nodiscard]] static std::expected<Json::Value, Error> Deserialize(
			Json::Value document, const ComponentRegistry& components = GetEngineComponentRegistry());
		[[nodiscard]] static std::expected<Json::Value, Error> DeserializeFromText(
			std::string_view text, const ComponentRegistry& components = GetEngineComponentRegistry());

		[[nodiscard]] static std::expected<void, Error> Save(
			const Scene& scene, Entity root, const std::filesystem::path& path);
		[[nodiscard]] static std::expected<Json::Value, Error> Load(
			const std::filesystem::path& path, const ComponentRegistry& components = GetEngineComponentRegistry());

		// Copies a prefab's entities into a scene, with new UUIDs, under parent (or as a root entity) at index among
		// its siblings. prefab is the prefab asset's UUID, recorded on the copy's root; nil for prefabs that aren't
		// assets
		[[nodiscard]] static std::expected<Entity, Error> Instantiate(Scene& scene, const Json::Value& entities,
			UUID prefab, Entity parent = {}, std::optional<size_t> index = std::nullopt);
	};

}
