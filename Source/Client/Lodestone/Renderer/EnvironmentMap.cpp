#include "Lodestone/Renderer/EnvironmentMap.h"

#include "Lodestone/Renderer/MipChain.h"
#include "Lodestone/Renderer/RenderResources.h"
#include "Lodestone/Renderer/ShaderTypes.h"

#include <glm/gtc/packing.hpp>
#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <string>
#include <vector>

namespace Lodestone {

	namespace {

		// The diffuse light is smooth, so the spherical harmonics need few pixels
		constexpr uint32_t IrradianceSourceWidth = 256;
		// The largest value a 16-bit float holds, short of infinity
		constexpr float MaxHalf = 65504.0f;
		constexpr uint32_t ThreadGroupSize = 8;

		uint32_t GetGroupCount(uint32_t size)
		{
			return (size + ThreadGroupSize - 1) / ThreadGroupSize;
		}

		nvrhi::TextureSubresourceSet GetMipFaces(uint32_t mip)
		{
			return {mip, 1, 0, 6};
		}

		nvrhi::TextureHandle CreateCubeMap(
			nvrhi::IDevice* device, uint32_t faceSize, uint32_t mipCount, const char* name)
		{
			nvrhi::TextureDesc desc;
			desc.width = faceSize;
			desc.height = faceSize;
			desc.arraySize = 6;
			desc.mipLevels = mipCount;
			desc.dimension = nvrhi::TextureDimension::TextureCube;
			desc.format = nvrhi::Format::RGBA16_FLOAT;
			desc.debugName = name;
			desc.isUAV = true;
			desc.initialState = nvrhi::ResourceStates::ShaderResource;
			desc.keepInitialState = true;
			return device->createTexture(desc);
		}

	}

	std::expected<Ref<EnvironmentMap>, Error> EnvironmentMap::Create(const RenderResources& resources,
		nvrhi::ICommandList* commandList, const HdrImage& image, std::string_view debugName)
	{
		if (image.IsEmpty())
			return std::unexpected(Error(ErrorCode::InvalidArgument, "An empty image can't be an environment"));
		nvrhi::IDevice* device = resources.GetDevice();
		auto map = CreateRef<EnvironmentMap>(Passkey());

		const HdrImage source = DownsampleHdr(image, MaxSourceWidth);
		map->m_Irradiance = ComputeIrradiance(DownsampleHdr(source, IrradianceSourceWidth));

		// The source as 16-bit floats, which every GPU filters
		std::vector<uint16_t> halves;
		halves.reserve(source.GetPixels().size());
		for (const float value : source.GetPixels())
			halves.push_back(glm::packHalf1x16(std::isfinite(value) ? std::clamp(value, 0.0f, MaxHalf) : 0.0f));
		const std::string equirectangularName = fmt::format("{} (equirectangular)", debugName);
		nvrhi::TextureDesc equirectangularDesc;
		equirectangularDesc.width = source.GetWidth();
		equirectangularDesc.height = source.GetHeight();
		equirectangularDesc.format = nvrhi::Format::RGBA16_FLOAT;
		equirectangularDesc.debugName = equirectangularName;
		equirectangularDesc.initialState = nvrhi::ResourceStates::ShaderResource;
		equirectangularDesc.keepInitialState = true;
		const nvrhi::TextureHandle equirectangular = device->createTexture(equirectangularDesc);

		// A cube face covers a quarter of the image's width
		const uint32_t faceSize =
			std::clamp(std::bit_ceil(std::max(source.GetWidth() / 4, 1u)), MinFaceSize, MaxFaceSize);
		const uint32_t cubeMipCount = GetMipCount(faceSize, faceSize);
		const uint32_t prefilteredSize = std::min(faceSize, MaxPrefilteredFaceSize);
		const uint32_t prefilteredMipCount =
			std::min(PrefilteredMipCount, GetMipCount(prefilteredSize, prefilteredSize));
		const std::string cubeName = fmt::format("{} (cube map)", debugName);
		const std::string prefilteredName = fmt::format("{} (prefiltered)", debugName);
		map->m_CubeMap = CreateCubeMap(device, faceSize, cubeMipCount, cubeName.c_str());
		map->m_PrefilteredMap = CreateCubeMap(device, prefilteredSize, prefilteredMipCount, prefilteredName.c_str());
		map->m_PrefilteredMaxLevel = prefilteredMipCount - 1;
		if (!equirectangular || !map->m_CubeMap || !map->m_PrefilteredMap)
			return std::unexpected(Error(
				ErrorCode::DeviceError, fmt::format("Creating the textures of environment '{}' failed", debugName)));
		commandList->writeTexture(equirectangular, 0, 0, halves.data(), static_cast<size_t>(source.GetWidth()) * 8);

		const auto dispatch = [&](nvrhi::IComputePipeline* pipeline, nvrhi::IBindingLayout* layout,
								  nvrhi::BindingSetDesc bindings, const ShaderTypes::EnvironmentConstants& constants)
		{
			bindings.addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(constants)));
			const nvrhi::BindingSetHandle set = device->createBindingSet(bindings, layout);
			if (!set)
				return false;
			commandList->setComputeState(nvrhi::ComputeState().setPipeline(pipeline).addBindingSet(set));
			commandList->setPushConstants(&constants, sizeof(constants));
			const uint32_t groups = GetGroupCount(constants.FaceSize);
			commandList->dispatch(groups, groups, 6);
			return true;
		};

		// The cube map's first level, from the image
		bool dispatched =
			dispatch(resources.GetEquirectangularToCubePipeline(), resources.GetEquirectangularToCubeLayout(),
				nvrhi::BindingSetDesc()
					.addItem(nvrhi::BindingSetItem::Texture_SRV(0, equirectangular))
					.addItem(nvrhi::BindingSetItem::Sampler(0, resources.GetEquirectangularSampler()))
					.addItem(nvrhi::BindingSetItem::Texture_UAV(0, map->m_CubeMap, nvrhi::Format::UNKNOWN,
						GetMipFaces(0), nvrhi::TextureDimension::Texture2DArray)),
				{.FaceSize = faceSize});

		// Each further level from the one above
		for (uint32_t mip = 1; mip < cubeMipCount && dispatched; ++mip)
		{
			dispatched = dispatch(resources.GetDownsampleCubePipeline(), resources.GetDownsampleCubeLayout(),
				nvrhi::BindingSetDesc()
					.addItem(nvrhi::BindingSetItem::Texture_SRV(1, map->m_CubeMap, nvrhi::Format::UNKNOWN,
						GetMipFaces(mip - 1), nvrhi::TextureDimension::Texture2DArray))
					.addItem(nvrhi::BindingSetItem::Texture_UAV(0, map->m_CubeMap, nvrhi::Format::UNKNOWN,
						GetMipFaces(mip), nvrhi::TextureDimension::Texture2DArray)),
				{.FaceSize = std::max(faceSize >> mip, 1u)});
		}

		// Each level of the prefiltered map, rougher as they shrink
		for (uint32_t level = 0; level < prefilteredMipCount && dispatched; ++level)
		{
			dispatched = dispatch(resources.GetPrefilterPipeline(), resources.GetPrefilterLayout(),
				nvrhi::BindingSetDesc()
					.addItem(nvrhi::BindingSetItem::Texture_SRV(2, map->m_CubeMap, nvrhi::Format::UNKNOWN,
						nvrhi::AllSubresources, nvrhi::TextureDimension::TextureCube))
					.addItem(nvrhi::BindingSetItem::Sampler(0, resources.GetLinearClampSampler()))
					.addItem(nvrhi::BindingSetItem::Texture_UAV(0, map->m_PrefilteredMap, nvrhi::Format::UNKNOWN,
						GetMipFaces(level), nvrhi::TextureDimension::Texture2DArray)),
				{
					.FaceSize = std::max(prefilteredSize >> level, 1u),
					.SampleCount = PrefilterSampleCount,
					.Roughness = prefilteredMipCount > 1
						? static_cast<float>(level) / static_cast<float>(prefilteredMipCount - 1)
						: 0.0f,
					.SourceFaceSize = static_cast<float>(faceSize),
					.SourceMipCount = static_cast<float>(cubeMipCount),
				});
		}
		if (!dispatched)
			return std::unexpected(Error(ErrorCode::DeviceError,
				fmt::format("Creating the binding sets of environment '{}' failed", debugName)));

		map->m_Bindings = device->createBindingSet(
			nvrhi::BindingSetDesc()
				.addItem(nvrhi::BindingSetItem::Texture_SRV(0, map->m_PrefilteredMap, nvrhi::Format::UNKNOWN,
					nvrhi::AllSubresources, nvrhi::TextureDimension::TextureCube))
				.addItem(nvrhi::BindingSetItem::Texture_SRV(1, resources.GetBrdfLut()))
				.addItem(nvrhi::BindingSetItem::Texture_SRV(2, map->m_CubeMap, nvrhi::Format::UNKNOWN,
					nvrhi::AllSubresources, nvrhi::TextureDimension::TextureCube))
				.addItem(nvrhi::BindingSetItem::Sampler(0, resources.GetLinearClampSampler())),
			resources.GetEnvironmentLayout());
		if (!map->m_Bindings)
			return std::unexpected(Error(
				ErrorCode::DeviceError, fmt::format("Creating the binding set of environment '{}' failed", debugName)));
		return map;
	}

	EnvironmentMap::EnvironmentMap(Passkey /*passkey*/)
	{
	}

}
