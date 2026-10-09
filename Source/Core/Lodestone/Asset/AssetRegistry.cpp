#include "Lodestone/Asset/AssetRegistry.h"

#include "Lodestone/Core/FileSystem.h"
#include "Lodestone/Core/Log.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/std.h>

#include <algorithm>
#include <set>
#include <system_error>

namespace Lodestone {

	namespace {

		bool IsHidden(const std::filesystem::path& name)
		{
			const std::u8string text = name.u8string();
			return !text.empty() && text.front() == u8'.';
		}

		// Every file under the directory, sorted so scans are deterministic, skipping hidden files and directories
		std::expected<std::vector<std::filesystem::path>, Error> ListFiles(const std::filesystem::path& directory)
		{
			std::error_code error;
			std::filesystem::recursive_directory_iterator iterator(
				directory, std::filesystem::directory_options::skip_permission_denied, error);
			if (error)
				return std::unexpected(
					Error(ErrorCode::IoError, fmt::format("Can't read the asset directory {}: {}", directory, error)));

			std::vector<std::filesystem::path> files;
			for (; iterator != std::filesystem::recursive_directory_iterator(); iterator.increment(error))
			{
				if (error)
					return std::unexpected(Error(
						ErrorCode::IoError, fmt::format("Can't read the asset directory {}: {}", directory, error)));
				const std::filesystem::directory_entry& entry = *iterator;
				if (IsHidden(entry.path().filename()))
				{
					if (entry.is_directory(error))
						iterator.disable_recursion_pending();
					continue;
				}
				if (entry.is_regular_file(error))
					files.push_back(entry.path());
			}
			if (error)
				return std::unexpected(
					Error(ErrorCode::IoError, fmt::format("Can't read the asset directory {}: {}", directory, error)));
			std::ranges::sort(files);
			return files;
		}

	}

	AssetRegistry::AssetRegistry(std::filesystem::path assetDirectory)
		: m_AssetDirectory(std::move(assetDirectory))
	{
	}

	std::expected<AssetScanReport, Error> AssetRegistry::Scan()
	{
		std::error_code error;
		if (!std::filesystem::is_directory(m_AssetDirectory, error))
			return std::unexpected(
				Error(ErrorCode::FileNotFound, fmt::format("The asset directory {} doesn't exist", m_AssetDirectory)));
		const auto files = ListFiles(m_AssetDirectory);
		if (!files)
			return std::unexpected(files.error());

		const std::set<std::filesystem::path> fileSet(files->begin(), files->end());
		AssetScanReport report;
		std::unordered_map<UUID, AssetInfo> assets;
		std::unordered_map<std::string, UUID> idsByPath;

		for (const std::filesystem::path& file : *files)
		{
			const std::string relativePath = PathToUtf8(file.lexically_relative(m_AssetDirectory));
			if (file.extension() == ".meta")
			{
				std::filesystem::path assetPath = file;
				assetPath.replace_extension();
				if (!fileSet.contains(assetPath))
					report.OrphanedMetadata.push_back(relativePath);
				continue;
			}

			const std::optional<AssetType> type = GetAssetTypeForFile(file);
			if (!type)
				continue;

			const std::filesystem::path metadataPath = AssetMetadataSerializer::GetMetadataPath(file);
			AssetMetadata metadata;
			if (fileSet.contains(metadataPath))
			{
				auto loaded = AssetMetadataSerializer::Load(metadataPath);
				if (!loaded)
				{
					report.Errors.push_back(std::move(loaded).error());
					continue;
				}
				if (loaded->Type != *type)
				{
					report.Errors.emplace_back(ErrorCode::InvalidArgument,
						fmt::format("{} is a {} file, but its metadata says it's a {}", relativePath, ToString(*type),
							ToString(loaded->Type)));
					continue;
				}
				metadata = std::move(*loaded);

				// Files are visited in sorted order, so the same copy keeps its UUID every time
				if (assets.contains(metadata.Id))
				{
					do
						metadata.Id = UUID::Generate();
					while (assets.contains(metadata.Id));
					if (auto saved = AssetMetadataSerializer::Save(metadata, metadataPath); !saved)
					{
						report.Errors.push_back(saved.error());
						continue;
					}
					report.ReassignedIds.push_back(relativePath);
				}
			}
			else
			{
				metadata.Type = *type;
				do
					metadata.Id = UUID::Generate();
				while (assets.contains(metadata.Id));
				if (auto saved = AssetMetadataSerializer::Save(metadata, metadataPath); !saved)
				{
					report.Errors.push_back(saved.error());
					continue;
				}
				report.CreatedMetadata.push_back(relativePath);
			}

			idsByPath.emplace(relativePath, metadata.Id);
			const UUID id = metadata.Id;
			assets.emplace(id, AssetInfo{.Path = relativePath, .Metadata = std::move(metadata)});
		}

		for (const Error& problem : report.Errors)
			LS_CORE_WARN("Skipped an asset: {}", problem);
		m_Assets = std::move(assets);
		m_IdsByPath = std::move(idsByPath);
		return report;
	}

	const AssetInfo* AssetRegistry::Find(UUID id) const
	{
		const auto asset = m_Assets.find(id);
		return asset != m_Assets.end() ? &asset->second : nullptr;
	}

	std::vector<const AssetInfo*> AssetRegistry::GetAll() const
	{
		std::vector<const AssetInfo*> assets;
		assets.reserve(m_Assets.size());
		for (const auto& [id, asset] : m_Assets)
			assets.push_back(&asset);
		std::ranges::sort(assets, {}, &AssetInfo::Path);
		return assets;
	}

	const AssetInfo* AssetRegistry::FindByPath(const std::filesystem::path& path) const
	{
		const auto id = m_IdsByPath.find(PathToUtf8(path.lexically_normal()));
		return id != m_IdsByPath.end() ? Find(id->second) : nullptr;
	}

	std::filesystem::path AssetRegistry::GetAbsolutePath(const AssetInfo& asset) const
	{
		return m_AssetDirectory / PathFromUtf8(asset.Path);
	}

}
