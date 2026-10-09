#pragma once

#include "Lodestone/Asset/AssetMetadata.h"
#include "Lodestone/Core/Error.h"
#include "Lodestone/Core/UUID.h"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Lodestone {

	// An asset the registry knows
	struct AssetInfo
	{
		// Relative to the asset directory, in UTF-8 with forward slashes (see PathToUtf8)
		std::string Path;
		AssetMetadata Metadata;
	};

	// What a scan found and changed
	struct AssetScanReport
	{
		// Assets that had no metadata file, which the scan created
		std::vector<std::string> CreatedMetadata;
		// Assets whose metadata had the UUID of another asset - usually a copied file - which got a new UUID
		std::vector<std::string> ReassignedIds;
		// Metadata files whose asset no longer exists. They're left alone: the asset may only be moved for now
		std::vector<std::string> OrphanedMetadata;
		// Assets that were skipped, because their metadata couldn't be read or doesn't match the file
		std::vector<Error> Errors;
	};

	// Maps asset UUIDs to the files in a project's asset directory (see docs/Architecture.md#asset-system). Every asset
	// file has a metadata file next to it ("<file>.meta") with its UUID and import settings; files refer to each other
	// by UUID, never by path, so assets can move without breaking references - as long as the metadata file moves too.
	//
	// Used from one thread at a time
	class AssetRegistry
	{
	public:
		explicit AssetRegistry(std::filesystem::path assetDirectory);

		// Finds every asset in the directory and its subdirectories, creating metadata for new ones, and replaces what
		// the registry knew. Files and directories whose names start with '.' are ignored. Fails only if the directory
		// can't be read at all; problems with single assets are listed in the report
		[[nodiscard]] std::expected<AssetScanReport, Error> Scan();

		const AssetInfo* Find(UUID id) const;
		// An asset by its path relative to the asset directory
		const AssetInfo* FindByPath(const std::filesystem::path& path) const;
		size_t GetAssetCount() const { return m_Assets.size(); }
		// Every asset, by path
		std::vector<const AssetInfo*> GetAll() const;

		const std::filesystem::path& GetAssetDirectory() const { return m_AssetDirectory; }
		std::filesystem::path GetAbsolutePath(const AssetInfo& asset) const;

	private:
		std::filesystem::path m_AssetDirectory;
		std::unordered_map<UUID, AssetInfo> m_Assets;
		std::unordered_map<std::string, UUID> m_IdsByPath;
	};

}
