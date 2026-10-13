#include "Lodestone/Asset/ModelCache.h"

#include "Lodestone/Asset/AssetRegistry.h"
#include "Lodestone/Asset/ModelImporter.h"
#include "Lodestone/Core/Log.h"

#include <spdlog/fmt/fmt.h>

#include <system_error>
#include <utility>

namespace Lodestone {

	ModelCache::ModelCache(const AssetRegistry& registry)
		: m_Registry(&registry)
	{
	}

	std::expected<const Model*, Error> ModelCache::Load(UUID id)
	{
		if (const auto found = m_Models.find(id); found != m_Models.end())
		{
			const Entry& entry = found->second;
			if (const std::optional<Model>& loaded = entry.Loaded)
				return &*loaded;
			return std::unexpected(entry.Failure.value_or(Error(ErrorCode::Unknown, "The model failed to load")));
		}

		Entry& entry = m_Models[id];
		const auto import = [this, id, &entry]() -> std::expected<Model, Error>
		{
			const AssetInfo* info = m_Registry->Find(id);
			if (info == nullptr)
				return std::unexpected(
					Error(ErrorCode::NotFound, fmt::format("The project has no asset with the ID {}", id)));
			if (info->Metadata.Type != AssetType::Model)
				return std::unexpected(Error(ErrorCode::InvalidArgument,
					fmt::format("Asset '{}' is a {}, not a model", info->Path, ToString(info->Metadata.Type))));
			const std::filesystem::path path = m_Registry->GetAbsolutePath(*info);
			std::error_code error;
			entry.WriteTime = std::filesystem::last_write_time(path, error);
			return ModelImporter::Import(path, {.LoadTextures = false});
		};
		auto model = import();
		if (!model)
		{
			LS_CORE_WARN("Can't load model {}: {}", id, model.error());
			entry.Failure = model.error();
			return std::unexpected(*entry.Failure);
		}
		return &entry.Loaded.emplace(std::move(*model));
	}

	void ModelCache::Refresh()
	{
		std::erase_if(m_Models,
			[this](const auto& cached)
			{
				const auto& [id, entry] = cached;
				if (entry.Failure)
					return true;
				const AssetInfo* info = m_Registry->Find(id);
				if (info == nullptr)
					return true;
				std::error_code error;
				const auto writeTime = std::filesystem::last_write_time(m_Registry->GetAbsolutePath(*info), error);
				return error || writeTime != entry.WriteTime;
			});
	}

}
