#include "Lodestone/Renderer/RenderAssets.h"

#include "Lodestone/Asset/AssetRegistry.h"
#include "Lodestone/Asset/ModelImporter.h"
#include "Lodestone/Core/Log.h"
#include "Lodestone/Renderer/ImageBasedLighting.h"

#include <spdlog/fmt/fmt.h>

#include <string>
#include <utility>

namespace Lodestone {

	namespace {

		// The registry's entry for an asset of a type, or why there's none
		std::expected<const AssetInfo*, Error> FindAsset(const AssetRegistry& registry, UUID id, AssetType type)
		{
			const AssetInfo* info = registry.Find(id);
			if (info == nullptr)
				return std::unexpected(Error(ErrorCode::NotFound, fmt::format("No asset has the ID {}", id)));
			if (info->Metadata.Type != type)
				return std::unexpected(Error(ErrorCode::InvalidArgument,
					fmt::format(
						"Asset '{}' is a {}, not a {}", info->Path, ToString(info->Metadata.Type), ToString(type))));
			return info;
		}

	}

	RenderAssetSource MakeRenderAssetSource(const AssetRegistry& registry)
	{
		return {
			.LoadModel = [&registry](UUID id) -> std::expected<Model, Error>
			{
				const auto info = FindAsset(registry, id, AssetType::Model);
				if (!info)
					return std::unexpected(info.error());
				return ModelImporter::Import(registry.GetAbsolutePath(**info));
			},
			.LoadEnvironment = [&registry](UUID id) -> std::expected<HdrImage, Error>
			{
				const auto info = FindAsset(registry, id, AssetType::Environment);
				if (!info)
					return std::unexpected(info.error());
				return HdrImage::Load(registry.GetAbsolutePath(**info));
			},
		};
	}

	RenderAssets::RenderAssets(const RenderResources& resources)
		: m_Resources(&resources)
	{
	}

	void RenderAssets::SetSource(RenderAssetSource source)
	{
		m_Source = std::move(source);
		Clear();
	}

	const GpuModel* RenderAssets::GetModel(UUID id, nvrhi::ICommandList* commandList)
	{
		if (id.IsNil())
			return nullptr;
		if (const auto found = m_Models.find(id); found != m_Models.end())
			return found->second.Asset.get();

		Entry<GpuModel>& entry = m_Models[id];
		auto model = m_Source.LoadModel ? m_Source.LoadModel(id)
										: std::expected<Model, Error>(std::unexpected(
											  Error(ErrorCode::NotFound, "The renderer has no asset source")));
		if (model)
		{
			auto gpuModel = GpuModel::Create(*m_Resources, commandList, std::move(*model), fmt::format("Model {}", id));
			if (gpuModel)
				entry.Asset = std::move(*gpuModel);
			else
				entry.Failure = gpuModel.error();
		}
		else
		{
			entry.Failure = model.error();
		}

		if (entry.Failure)
			LS_CORE_ERROR("Can't draw model {}: {}", id, *entry.Failure);
		return entry.Asset.get();
	}

	const GpuModel* RenderAssets::FindModel(UUID id) const
	{
		const auto found = m_Models.find(id);
		return found != m_Models.end() ? found->second.Asset.get() : nullptr;
	}

	const EnvironmentMap* RenderAssets::GetEnvironment(UUID id, nvrhi::ICommandList* commandList)
	{
		if (id.IsNil())
			return GetDefaultEnvironment(commandList);
		if (const auto found = m_Environments.find(id); found != m_Environments.end())
			return found->second.Asset ? found->second.Asset.get() : GetDefaultEnvironment(commandList);

		Entry<EnvironmentMap>& entry = m_Environments[id];
		auto image = m_Source.LoadEnvironment ? m_Source.LoadEnvironment(id)
											  : std::expected<HdrImage, Error>(std::unexpected(
													Error(ErrorCode::NotFound, "The renderer has no asset source")));
		if (image)
		{
			auto map = EnvironmentMap::Create(*m_Resources, commandList, *image, fmt::format("Environment {}", id));
			if (map)
				entry.Asset = std::move(*map);
			else
				entry.Failure = map.error();
		}
		else
		{
			entry.Failure = image.error();
		}

		if (entry.Failure)
		{
			LS_CORE_ERROR(
				"Can't light the scene with environment {}, so the default one lights it: {}", id, *entry.Failure);
			return GetDefaultEnvironment(commandList);
		}
		return entry.Asset.get();
	}

	const EnvironmentMap* RenderAssets::GetDefaultEnvironment(nvrhi::ICommandList* commandList)
	{
		if (m_DefaultEnvironment || m_DefaultEnvironmentFailed)
			return m_DefaultEnvironment.get();
		auto map = EnvironmentMap::Create(*m_Resources, commandList, MakeDefaultEnvironment(), "Default environment");
		if (!map)
		{
			LS_CORE_ERROR("Can't create the default environment: {}", map.error());
			m_DefaultEnvironmentFailed = true;
			return nullptr;
		}
		m_DefaultEnvironment = std::move(*map);
		return m_DefaultEnvironment.get();
	}

	const Error* RenderAssets::GetError(UUID id) const
	{
		if (const auto found = m_Models.find(id); found != m_Models.end())
		{
			if (const std::optional<Error>& failure = found->second.Failure)
				return &*failure;
		}
		if (const auto found = m_Environments.find(id); found != m_Environments.end())
		{
			if (const std::optional<Error>& failure = found->second.Failure)
				return &*failure;
		}
		return nullptr;
	}

	void RenderAssets::Invalidate(UUID id)
	{
		m_Models.erase(id);
		m_Environments.erase(id);
	}

	void RenderAssets::ForgetFailures()
	{
		std::erase_if(m_Models, [](const auto& entry) { return entry.second.Failure.has_value(); });
		std::erase_if(m_Environments, [](const auto& entry) { return entry.second.Failure.has_value(); });
	}

	void RenderAssets::Clear()
	{
		m_Models.clear();
		m_Environments.clear();
	}

}
