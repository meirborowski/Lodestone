#pragma once

#include "Lodestone/Asset/Model.h"
#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/Error.h"

#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <expected>

namespace Lodestone {

	class GraphicsDevice;

	// The device objects the scene renderer's parts share: the binding layouts of its three descriptor sets (the view,
	// the environment and the material - see Shaders/Common.hlsli and Shaders/Mesh.hlsl), samplers, a white texture
	// for material slots without one, the BRDF lookup table, and the compute pipelines that prepare environment maps
	class RenderResources
	{
	private:
		struct Passkey
		{
			explicit Passkey() = default;
		};

	public:
		// The descriptor set (and HLSL register space) of each layout
		static constexpr uint32_t ViewSpace = 0;
		static constexpr uint32_t EnvironmentSpace = 1;
		static constexpr uint32_t MaterialSpace = 2;
		// The texture slots of a material's binding set, in Mesh.hlsl's order
		static constexpr uint32_t MaterialTextureCount = 5;
		static constexpr uint32_t BrdfLutSize = 64;

		[[nodiscard]] static std::expected<Scope<RenderResources>, Error> Create(const GraphicsDevice& device);

		RenderResources(Passkey passkey, nvrhi::IDevice* device, float maxAnisotropy);

		nvrhi::IDevice* GetDevice() const { return m_Device; }

		nvrhi::IBindingLayout* GetViewLayout() const { return m_ViewLayout; }
		nvrhi::IBindingLayout* GetEnvironmentLayout() const { return m_EnvironmentLayout; }
		// Includes the draw's push constants (ShaderTypes::DrawConstants)
		nvrhi::IBindingLayout* GetMaterialLayout() const { return m_MaterialLayout; }

		// A trilinear sampler for a material texture's wrap modes, or a nearest-neighbor one, with anisotropic
		// filtering where the device has it
		nvrhi::ISampler* GetMaterialSampler(TextureWrap wrapU, TextureWrap wrapV, bool nearest) const;
		nvrhi::ISampler* GetLinearClampSampler() const { return m_LinearClampSampler; }
		nvrhi::ISampler* GetPointClampSampler() const { return m_PointClampSampler; }
		// A comparison sampler for shadow maps: returns how much of a 2x2 texel footprint is farther than the
		// reference depth
		nvrhi::ISampler* GetShadowSampler() const { return m_ShadowSampler; }
		// Wraps horizontally and clamps vertically, for equirectangular images
		nvrhi::ISampler* GetEquirectangularSampler() const { return m_EquirectangularSampler; }

		// One opaque white texel
		nvrhi::ITexture* GetWhiteTexture() const { return m_WhiteTexture; }
		// ImageBasedLighting.h's ComputeBrdfLut(), as two 16-bit float channels
		nvrhi::ITexture* GetBrdfLut() const { return m_BrdfLut; }

		// The compute pipelines of Shaders/Environment.hlsl, each with its binding layout
		nvrhi::IComputePipeline* GetEquirectangularToCubePipeline() const { return m_EquirectangularToCube; }
		nvrhi::IBindingLayout* GetEquirectangularToCubeLayout() const { return m_EquirectangularToCubeLayout; }
		nvrhi::IComputePipeline* GetDownsampleCubePipeline() const { return m_DownsampleCube; }
		nvrhi::IBindingLayout* GetDownsampleCubeLayout() const { return m_DownsampleCubeLayout; }
		nvrhi::IComputePipeline* GetPrefilterPipeline() const { return m_Prefilter; }
		nvrhi::IBindingLayout* GetPrefilterLayout() const { return m_PrefilterLayout; }

	private:
		[[nodiscard]] std::expected<void, Error> CreateLayouts();
		[[nodiscard]] std::expected<void, Error> CreateSamplers();
		[[nodiscard]] std::expected<void, Error> CreateTextures();
		[[nodiscard]] std::expected<void, Error> CreateEnvironmentPipelines();

	private:
		nvrhi::IDevice* m_Device;
		float m_MaxAnisotropy;

		nvrhi::BindingLayoutHandle m_ViewLayout;
		nvrhi::BindingLayoutHandle m_EnvironmentLayout;
		nvrhi::BindingLayoutHandle m_MaterialLayout;

		// By wrap U and wrap V (TextureWrap has three modes each) and filtering (linear or nearest): 3 x 3 x 2
		std::array<nvrhi::SamplerHandle, 18> m_MaterialSamplers;
		nvrhi::SamplerHandle m_LinearClampSampler;
		nvrhi::SamplerHandle m_PointClampSampler;
		nvrhi::SamplerHandle m_ShadowSampler;
		nvrhi::SamplerHandle m_EquirectangularSampler;

		nvrhi::TextureHandle m_WhiteTexture;
		nvrhi::TextureHandle m_BrdfLut;

		nvrhi::ShaderHandle m_EquirectangularToCubeShader;
		nvrhi::ShaderHandle m_DownsampleCubeShader;
		nvrhi::ShaderHandle m_PrefilterShader;
		nvrhi::BindingLayoutHandle m_EquirectangularToCubeLayout;
		nvrhi::BindingLayoutHandle m_DownsampleCubeLayout;
		nvrhi::BindingLayoutHandle m_PrefilterLayout;
		nvrhi::ComputePipelineHandle m_EquirectangularToCube;
		nvrhi::ComputePipelineHandle m_DownsampleCube;
		nvrhi::ComputePipelineHandle m_Prefilter;
	};

}
