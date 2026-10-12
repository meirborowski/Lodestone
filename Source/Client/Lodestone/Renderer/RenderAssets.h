#pragma once

#include "Lodestone/Asset/Model.h"
#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/Error.h"
#include "Lodestone/Core/HdrImage.h"
#include "Lodestone/Core/UUID.h"
#include "Lodestone/Renderer/EnvironmentMap.h"
#include "Lodestone/Renderer/GpuModel.h"

#include <nvrhi/nvrhi.h>

#include <expected>
#include <functional>
#include <optional>
#include <unordered_map>

namespace Lodestone {

	class AssetRegistry;
	class RenderResources;

	// Where the renderer gets the assets scenes refer to, by UUID
	struct RenderAssetSource
	{
		std::function<std::expected<Model, Error>(UUID)> LoadModel;
		std::function<std::expected<HdrImage, Error>(UUID)> LoadEnvironment;
	};

	// Loads assets from a project's asset registry, importing the files they map to. The registry must outlive the
	// source, which reads it on every load
	RenderAssetSource MakeRenderAssetSource(const AssetRegistry& registry);

	// The models and environments the renderer has on the GPU, by asset UUID. Each is loaded from the source and
	// uploaded the first time a scene uses it, recording the upload into the frame's command list. Assets that fail to
	// load are remembered, and their error logged once, so they aren't tried again every frame; they draw nothing, and
	// environments fall back to the default one.
	//
	// Used from the thread that renders
	class RenderAssets
	{
	public:
		explicit RenderAssets(const RenderResources& resources);

		// Replaces where assets come from, and forgets every asset loaded from the old source
		void SetSource(RenderAssetSource source);

		// The model asset on the GPU, loaded if it isn't yet; null if it can't be
		const GpuModel* GetModel(UUID id, nvrhi::ICommandList* commandList);
		// The model asset if it's loaded already; null otherwise
		const GpuModel* FindModel(UUID id) const;
		// The environment asset on the GPU, loaded if it isn't yet; the default environment for the nil UUID, or if
		// it can't be loaded
		const EnvironmentMap* GetEnvironment(UUID id, nvrhi::ICommandList* commandList);
		// The environment of scenes without one (ImageBasedLighting.h's MakeDefaultEnvironment()); null only if the
		// GPU can't create it
		const EnvironmentMap* GetDefaultEnvironment(nvrhi::ICommandList* commandList);

		// Why an asset failed to load, if it did
		const Error* GetError(UUID id) const;
		// Forgets an asset, so it's loaded again the next time it's used - after its file changes
		void Invalidate(UUID id);
		// Forgets the assets that failed to load, so they're tried again the next time they're used - after the
		// source's assets change, which may have added or fixed them
		void ForgetFailures();
		// Forgets every asset
		void Clear();

	private:
		template <typename T>
		struct Entry
		{
			Ref<T> Asset;
			std::optional<Error> Failure;
		};

	private:
		const RenderResources* m_Resources;
		RenderAssetSource m_Source;
		std::unordered_map<UUID, Entry<GpuModel>> m_Models;
		std::unordered_map<UUID, Entry<EnvironmentMap>> m_Environments;
		Ref<EnvironmentMap> m_DefaultEnvironment;
		bool m_DefaultEnvironmentFailed = false;
	};

}
