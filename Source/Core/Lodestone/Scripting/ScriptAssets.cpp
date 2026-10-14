#include "Lodestone/Scripting/ScriptAssets.h"

#include "Lodestone/Asset/AssetRegistry.h"
#include "Lodestone/Core/FileSystem.h"
#include "Lodestone/Scene/PrefabSerializer.h"

#include <spdlog/fmt/fmt.h>

#include <system_error>

namespace Lodestone {

	namespace {

		// The file's write time, mixed with its size: file systems can give two writes close together the same time
		std::optional<uint64_t> GetFileVersion(const std::filesystem::path& path)
		{
			std::error_code error;
			const auto time = std::filesystem::last_write_time(path, error);
			if (error)
				return std::nullopt;
			const uintmax_t size = std::filesystem::file_size(path, error);
			if (error)
				return std::nullopt;
			const auto ticks = static_cast<uint64_t>(time.time_since_epoch().count());
			return ticks ^ (static_cast<uint64_t>(size) * 0x9E3779B97F4A7C15ull);
		}

	}

	std::optional<std::string> ProjectScriptAssets::FindScript(UUID id) const
	{
		const AssetInfo* info = m_Registry->Find(id);
		if (info == nullptr || info->Metadata.Type != AssetType::Script)
			return std::nullopt;
		return info->Path;
	}

	std::expected<ScriptFile, Error> ProjectScriptAssets::ReadScript(std::string_view path) const
	{
		const AssetInfo* info = m_Registry->FindByPath(PathFromUtf8(path));
		if (info == nullptr || info->Metadata.Type != AssetType::Script)
			return std::unexpected(Error(ErrorCode::NotFound, fmt::format("The project has no script '{}'", path)));
		const std::filesystem::path file = m_Registry->GetAbsolutePath(*info);
		const std::optional<uint64_t> version = GetFileVersion(file);
		auto source = ReadFile(file, MaxScriptSize);
		if (!source)
			return std::unexpected(source.error());
		return ScriptFile{.Path = info->Path, .Source = std::move(*source), .Version = version.value_or(0)};
	}

	std::optional<uint64_t> ProjectScriptAssets::GetVersion(std::string_view path) const
	{
		const AssetInfo* info = m_Registry->FindByPath(PathFromUtf8(path));
		if (info == nullptr || info->Metadata.Type != AssetType::Script)
			return std::nullopt;
		return GetFileVersion(m_Registry->GetAbsolutePath(*info));
	}

	std::expected<ScriptPrefab, Error> ProjectScriptAssets::LoadPrefab(std::string_view pathOrId) const
	{
		const AssetInfo* info = nullptr;
		if (const std::optional<UUID> id = UUID::Parse(pathOrId))
			info = m_Registry->Find(*id);
		else
			info = m_Registry->FindByPath(PathFromUtf8(pathOrId));
		if (info == nullptr || info->Metadata.Type != AssetType::Prefab)
			return std::unexpected(Error(ErrorCode::NotFound, fmt::format("The project has no prefab '{}'", pathOrId)));
		auto entities = PrefabSerializer::Load(m_Registry->GetAbsolutePath(*info));
		if (!entities)
			return std::unexpected(entities.error());
		return ScriptPrefab{.Entities = std::move(*entities), .Prefab = info->Metadata.Id};
	}

}
