#pragma once

#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/Error.h"
#include "Lodestone/Scene/Scene.h"
#include "Lodestone/Serialization/FileFormat.h"
#include "Lodestone/Serialization/Json.h"

#include <expected>
#include <filesystem>
#include <string_view>

namespace Lodestone {

	// Saves and loads scenes as JSON documents (.lscene files). Every component the registry knows is saved, except
	// internal ones, with every field; entities are listed parents first, in hierarchy order:
	//
	//   {"format": "Lodestone.Scene", "version": 1, "entities": [
	//       {"components": {"ID": {"ID": "..."}, "Name": {"Name": "Player"}, "Transform": {...}, ...}}, ...]}
	//
	// Loading is strict: an unknown component or field, a value of the wrong type or out of range, a missing or
	// duplicate UUID, a missing parent or a cycle in the hierarchy fails the whole load, which never leaves a partly
	// loaded scene. Missing fields keep their defaults
	class SceneSerializer
	{
	public:
		static const FileFormat& GetFormat();

		static Json::Value Serialize(const Scene& scene);
		[[nodiscard]] static std::expected<Scope<Scene>, Error> Deserialize(
			Json::Value document, const ComponentRegistry& components = GetEngineComponentRegistry());

		// The scene as a document's text, and a scene from a document's text
		static std::string SerializeToText(const Scene& scene);
		[[nodiscard]] static std::expected<Scope<Scene>, Error> DeserializeFromText(
			std::string_view text, const ComponentRegistry& components = GetEngineComponentRegistry());

		[[nodiscard]] static std::expected<void, Error> Save(const Scene& scene, const std::filesystem::path& path);
		[[nodiscard]] static std::expected<Scope<Scene>, Error> Load(
			const std::filesystem::path& path, const ComponentRegistry& components = GetEngineComponentRegistry());
	};

}
