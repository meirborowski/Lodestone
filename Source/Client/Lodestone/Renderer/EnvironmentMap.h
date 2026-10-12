#pragma once

#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/Error.h"
#include "Lodestone/Core/HdrImage.h"
#include "Lodestone/Renderer/ImageBasedLighting.h"

#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <expected>
#include <string_view>

namespace Lodestone {

	class RenderResources;

	// An HDR environment on the GPU, ready for image-based lighting and the sky: the environment as a cube map with
	// mips, and prefiltered for specular reflections - one mip level per roughness, from mirror-like to fully rough.
	// Its diffuse light is spherical harmonics, computed on the CPU (ImageBasedLighting.h)
	class EnvironmentMap
	{
	private:
		struct Passkey
		{
			explicit Passkey() = default;
		};

	public:
		// Larger images are halved until they fit, before anything else
		static constexpr uint32_t MaxSourceWidth = 2048;
		static constexpr uint32_t MinFaceSize = 16;
		static constexpr uint32_t MaxFaceSize = 512;
		static constexpr uint32_t MaxPrefilteredFaceSize = 128;
		static constexpr uint32_t PrefilteredMipCount = 6;
		// Directions each texel of the prefiltered map averages
		static constexpr uint32_t PrefilterSampleCount = 64;

		// Creates the cube maps and records their preparation, on compute shaders, into an open command list
		[[nodiscard]] static std::expected<Ref<EnvironmentMap>, Error> Create(const RenderResources& resources,
			nvrhi::ICommandList* commandList, const HdrImage& image, std::string_view debugName);

		explicit EnvironmentMap(Passkey passkey);

		nvrhi::ITexture* GetCubeMap() const { return m_CubeMap; }
		nvrhi::ITexture* GetPrefilteredMap() const { return m_PrefilteredMap; }
		// The highest mip level of the prefiltered map, which holds roughness 1
		uint32_t GetPrefilteredMaxLevel() const { return m_PrefilteredMaxLevel; }
		const SphericalHarmonics& GetIrradiance() const { return m_Irradiance; }
		// The environment's descriptor set (RenderResources::GetEnvironmentLayout())
		nvrhi::IBindingSet* GetBindings() const { return m_Bindings; }

	private:
		nvrhi::TextureHandle m_CubeMap;
		nvrhi::TextureHandle m_PrefilteredMap;
		uint32_t m_PrefilteredMaxLevel = 0;
		SphericalHarmonics m_Irradiance{};
		nvrhi::BindingSetHandle m_Bindings;
	};

}
