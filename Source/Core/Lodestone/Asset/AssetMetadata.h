#pragma once

#include "Lodestone/Core/Error.h"
#include "Lodestone/Core/UUID.h"
#include "Lodestone/Serialization/FileFormat.h"
#include "Lodestone/Serialization/Json.h"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Lodestone {

	enum class AssetType
	{
		Scene,
		Prefab,
		Texture,
		Model,
		// An HDR environment map, for image-based lighting
		Environment,
		Audio,
		Font,
		Script,
	};

	std::string_view ToString(AssetType type);
	std::optional<AssetType> AssetTypeFromString(std::string_view name);
	// The asset type of a file, from its extension (".png", case-insensitive), or nothing if it isn't an asset
	std::optional<AssetType> GetAssetTypeForFile(const std::filesystem::path& path);

	// What the engine stores about an asset, in a metadata file next to it (see AssetMetadataSerializer)
	struct AssetMetadata
	{
		UUID Id;
		AssetType Type = AssetType::Scene;
		// How the asset's importer processes it. Each importer defines its own settings
		Json::Value ImportSettings = Json::Value::object();

		bool operator==(const AssetMetadata& other) const = default;
	};

	// Saves and loads asset metadata files, "<asset file>.meta":
	//
	//   {"format": "Lodestone.AssetMetadata", "version": 1, "id": "...", "type": "Texture", "importSettings": {}}
	class AssetMetadataSerializer
	{
	public:
		static const FileFormat& GetFormat();
		// The metadata file of an asset file
		static std::filesystem::path GetMetadataPath(const std::filesystem::path& assetPath);

		static std::string SerializeToText(const AssetMetadata& metadata);
		[[nodiscard]] static std::expected<AssetMetadata, Error> DeserializeFromText(std::string_view text);

		[[nodiscard]] static std::expected<void, Error> Save(
			const AssetMetadata& metadata, const std::filesystem::path& path);
		[[nodiscard]] static std::expected<AssetMetadata, Error> Load(const std::filesystem::path& path);
	};

}
