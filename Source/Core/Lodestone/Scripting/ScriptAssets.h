#pragma once

#include "Lodestone/Core/Error.h"
#include "Lodestone/Core/UUID.h"
#include "Lodestone/Serialization/Json.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace Lodestone {

	class AssetRegistry;

	struct ScriptFile
	{
		// Relative to the asset directory, with forward slashes: what errors name the script by
		std::string Path;
		std::string Source;
		// Changes when the file does (see ScriptAssets::GetVersion)
		uint64_t Version = 0;
	};

	struct ScriptPrefab
	{
		// The prefab's entities (see PrefabSerializer::Instantiate)
		Json::Value Entities;
		// The prefab asset, recorded on instances
		UUID Prefab;
	};

	// What scripts need of a game's assets: their source, and prefabs to instantiate. In the editor they're a project's
	// files; tests give them from memory
	class ScriptAssets
	{
	public:
		ScriptAssets() = default;
		virtual ~ScriptAssets() = default;

		ScriptAssets(const ScriptAssets&) = delete;
		ScriptAssets& operator=(const ScriptAssets&) = delete;
		ScriptAssets(ScriptAssets&&) = delete;
		ScriptAssets& operator=(ScriptAssets&&) = delete;

		// The path of a script asset, from its UUID; nothing if there's no such script
		virtual std::optional<std::string> FindScript(UUID id) const = 0;
		// A script by its path, such as "Scripts/Paddle.lua"
		[[nodiscard]] virtual std::expected<ScriptFile, Error> ReadScript(std::string_view path) const = 0;
		// A number that changes whenever the script's file does, without reading it; nothing if it's gone
		virtual std::optional<uint64_t> GetVersion(std::string_view path) const = 0;
		// A prefab, by its path or its UUID
		[[nodiscard]] virtual std::expected<ScriptPrefab, Error> LoadPrefab(std::string_view pathOrId) const = 0;
	};

	// A project's scripts and prefabs, through its asset registry, which must outlive this. Only the registry's assets
	// can be read, so scripts can't reach files outside the project
	class ProjectScriptAssets final : public ScriptAssets
	{
	public:
		explicit ProjectScriptAssets(const AssetRegistry& registry)
			: m_Registry(&registry)
		{
		}

		std::optional<std::string> FindScript(UUID id) const override;
		std::expected<ScriptFile, Error> ReadScript(std::string_view path) const override;
		std::optional<uint64_t> GetVersion(std::string_view path) const override;
		std::expected<ScriptPrefab, Error> LoadPrefab(std::string_view pathOrId) const override;

		// The largest script read, in bytes
		static constexpr size_t MaxScriptSize = size_t{4} * 1024 * 1024;

	private:
		const AssetRegistry* m_Registry;
	};

}
