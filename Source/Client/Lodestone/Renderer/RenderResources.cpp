#include "Lodestone/Renderer/RenderResources.h"

#include "Lodestone/Graphics/GraphicsDevice.h"
#include "Lodestone/Graphics/ShaderModule.h"
#include "Lodestone/Renderer/ImageBasedLighting.h"
#include "Lodestone/Renderer/ShaderTypes.h"

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <vector>

namespace Lodestone {

	namespace {

		// SPIR-V compiled from Shaders/Environment.hlsl at build time
#include "Shaders/Environment_CSDownsampleCube.spirv.h"
#include "Shaders/Environment_CSEquirectangularToCube.spirv.h"
#include "Shaders/Environment_CSPrefilter.spirv.h"

		// Enough samples for a smooth table; it's computed once per process
		constexpr uint32_t BrdfLutSampleCount = 512;
		// Material textures are sharp up close and stay so at grazing angles
		constexpr float MaterialAnisotropy = 8.0f;

		size_t GetSamplerIndex(TextureWrap wrapU, TextureWrap wrapV, bool nearest)
		{
			return (static_cast<size_t>(wrapU) * 3 + static_cast<size_t>(wrapV)) * 2 + (nearest ? 1 : 0);
		}

		nvrhi::SamplerAddressMode ToAddressMode(TextureWrap wrap)
		{
			switch (wrap)
			{
				case TextureWrap::Repeat:
					return nvrhi::SamplerAddressMode::Wrap;
				case TextureWrap::ClampToEdge:
					return nvrhi::SamplerAddressMode::Clamp;
				case TextureWrap::MirroredRepeat:
					return nvrhi::SamplerAddressMode::Mirror;
			}
			return nvrhi::SamplerAddressMode::Wrap;
		}

		// The table as 16-bit floats, computed the first time it's needed
		const std::vector<uint16_t>& GetBrdfLutTexels()
		{
			static const std::vector<uint16_t> Texels = []
			{
				const std::vector<glm::vec2> table = ComputeBrdfLut(RenderResources::BrdfLutSize, BrdfLutSampleCount);
				std::vector<uint16_t> halves;
				halves.reserve(table.size() * 2);
				for (const glm::vec2& value : table)
				{
					halves.push_back(glm::packHalf1x16(value.x));
					halves.push_back(glm::packHalf1x16(value.y));
				}
				return halves;
			}();
			return Texels;
		}

	}

	std::expected<Scope<RenderResources>, Error> RenderResources::Create(const GraphicsDevice& device)
	{
		auto resources = CreateScope<RenderResources>(
			Passkey(), device.GetNvrhiDevice(), std::min(device.GetInfo().MaxSamplerAnisotropy, MaterialAnisotropy));
		if (auto created = resources->CreateLayouts(); !created)
			return std::unexpected(created.error());
		if (auto created = resources->CreateSamplers(); !created)
			return std::unexpected(created.error());
		if (auto created = resources->CreateTextures(); !created)
			return std::unexpected(created.error());
		if (auto created = resources->CreateEnvironmentPipelines(); !created)
			return std::unexpected(created.error());
		return resources;
	}

	RenderResources::RenderResources(Passkey /*passkey*/, nvrhi::IDevice* device, float maxAnisotropy)
		: m_Device(device), m_MaxAnisotropy(maxAnisotropy)
	{
	}

	nvrhi::ISampler* RenderResources::GetMaterialSampler(TextureWrap wrapU, TextureWrap wrapV, bool nearest) const
	{
		return m_MaterialSamplers[GetSamplerIndex(wrapU, wrapV, nearest)];
	}

	std::expected<void, Error> RenderResources::CreateLayouts()
	{
		m_ViewLayout = m_Device->createBindingLayout(nvrhi::BindingLayoutDesc()
				.setVisibility(nvrhi::ShaderType::All)
				.setRegisterSpaceAndDescriptorSet(ViewSpace)
				.addItem(nvrhi::BindingLayoutItem::ConstantBuffer(0))
				.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0))
				.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1))
				.addItem(nvrhi::BindingLayoutItem::Texture_SRV(2))
				.addItem(nvrhi::BindingLayoutItem::Texture_SRV(3))
				.addItem(nvrhi::BindingLayoutItem::Sampler(0))
				.addItem(nvrhi::BindingLayoutItem::Sampler(1)));
		m_EnvironmentLayout = m_Device->createBindingLayout(nvrhi::BindingLayoutDesc()
				.setVisibility(nvrhi::ShaderType::All)
				.setRegisterSpaceAndDescriptorSet(EnvironmentSpace)
				.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
				.addItem(nvrhi::BindingLayoutItem::Texture_SRV(1))
				.addItem(nvrhi::BindingLayoutItem::Texture_SRV(2))
				.addItem(nvrhi::BindingLayoutItem::Sampler(0)));

		nvrhi::BindingLayoutDesc material;
		material.setVisibility(nvrhi::ShaderType::All)
			.setRegisterSpaceAndDescriptorSet(MaterialSpace)
			.addItem(nvrhi::BindingLayoutItem::ConstantBuffer(0));
		for (uint32_t slot = 0; slot < MaterialTextureCount; ++slot)
		{
			material.addItem(nvrhi::BindingLayoutItem::Texture_SRV(slot))
				.addItem(nvrhi::BindingLayoutItem::Sampler(slot));
		}
		// Push constants have no register; slot 1 keeps them apart from the constant buffer in slot 0
		material.addItem(nvrhi::BindingLayoutItem::PushConstants(1, sizeof(ShaderTypes::DrawConstants)));
		m_MaterialLayout = m_Device->createBindingLayout(material);
		m_SkinLayout = m_Device->createBindingLayout(nvrhi::BindingLayoutDesc()
				.setVisibility(nvrhi::ShaderType::Vertex)
				.setRegisterSpaceAndDescriptorSet(SkinSpace)
				.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0)));

		if (!m_ViewLayout || !m_EnvironmentLayout || !m_MaterialLayout || !m_SkinLayout)
			return std::unexpected(
				Error(ErrorCode::DeviceError, "Creating the scene renderer's binding layouts failed"));
		return {};
	}

	std::expected<void, Error> RenderResources::CreateSamplers()
	{
		constexpr std::array<TextureWrap, 3> wraps = {
			TextureWrap::Repeat, TextureWrap::ClampToEdge, TextureWrap::MirroredRepeat};
		for (const TextureWrap wrapU : wraps)
		{
			for (const TextureWrap wrapV : wraps)
			{
				for (const bool nearest : {false, true})
				{
					nvrhi::SamplerDesc desc;
					desc.setAddressU(ToAddressMode(wrapU)).setAddressV(ToAddressMode(wrapV));
					if (nearest)
						desc.setMinFilter(false).setMagFilter(false);
					else
						desc.setMaxAnisotropy(m_MaxAnisotropy);
					nvrhi::SamplerHandle sampler = m_Device->createSampler(desc);
					if (!sampler)
						return std::unexpected(Error(ErrorCode::DeviceError, "Creating a material sampler failed"));
					m_MaterialSamplers[GetSamplerIndex(wrapU, wrapV, nearest)] = std::move(sampler);
				}
			}
		}

		m_LinearClampSampler =
			m_Device->createSampler(nvrhi::SamplerDesc().setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));
		m_PointClampSampler = m_Device->createSampler(
			nvrhi::SamplerDesc().setAllAddressModes(nvrhi::SamplerAddressMode::Clamp).setAllFilters(false));
		m_ShadowSampler = m_Device->createSampler(nvrhi::SamplerDesc()
				.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
				.setMipFilter(false)
				.setReductionType(nvrhi::SamplerReductionType::Comparison));
		m_EquirectangularSampler = m_Device->createSampler(nvrhi::SamplerDesc()
				.setAddressU(nvrhi::SamplerAddressMode::Wrap)
				.setAddressV(nvrhi::SamplerAddressMode::Clamp)
				.setMipFilter(false));
		if (!m_LinearClampSampler || !m_PointClampSampler || !m_ShadowSampler || !m_EquirectangularSampler)
			return std::unexpected(Error(ErrorCode::DeviceError, "Creating the scene renderer's samplers failed"));
		return {};
	}

	std::expected<void, Error> RenderResources::CreateTextures()
	{
		nvrhi::TextureDesc white;
		white.width = 1;
		white.height = 1;
		white.format = nvrhi::Format::RGBA8_UNORM;
		white.debugName = "White texture";
		white.initialState = nvrhi::ResourceStates::ShaderResource;
		white.keepInitialState = true;
		m_WhiteTexture = m_Device->createTexture(white);

		nvrhi::TextureDesc lut;
		lut.width = BrdfLutSize;
		lut.height = BrdfLutSize;
		lut.format = nvrhi::Format::RG16_FLOAT;
		lut.debugName = "BRDF lookup table";
		lut.initialState = nvrhi::ResourceStates::ShaderResource;
		lut.keepInitialState = true;
		m_BrdfLut = m_Device->createTexture(lut);

		const nvrhi::CommandListHandle commandList = m_Device->createCommandList();
		if (!m_WhiteTexture || !m_BrdfLut || !commandList)
			return std::unexpected(Error(ErrorCode::DeviceError, "Creating the scene renderer's textures failed"));

		constexpr uint32_t whiteTexel = 0xFFFFFFFF;
		const std::vector<uint16_t>& lutTexels = GetBrdfLutTexels();
		commandList->open();
		commandList->writeTexture(m_WhiteTexture, 0, 0, &whiteTexel, sizeof(whiteTexel));
		commandList->writeTexture(
			m_BrdfLut, 0, 0, lutTexels.data(), static_cast<size_t>(BrdfLutSize) * 2 * sizeof(uint16_t));
		commandList->close();
		m_Device->executeCommandList(commandList);
		return {};
	}

	std::expected<void, Error> RenderResources::CreateEnvironmentPipelines()
	{
		m_EquirectangularToCubeShader = CreateShaderModule(m_Device, nvrhi::ShaderType::Compute,
			"CSEquirectangularToCube", g_Environment_CSEquirectangularToCube_spirv,
			sizeof(g_Environment_CSEquirectangularToCube_spirv), "Equirectangular to cube CS");
		m_DownsampleCubeShader = CreateShaderModule(m_Device, nvrhi::ShaderType::Compute, "CSDownsampleCube",
			g_Environment_CSDownsampleCube_spirv, sizeof(g_Environment_CSDownsampleCube_spirv), "Downsample cube CS");
		m_PrefilterShader = CreateShaderModule(m_Device, nvrhi::ShaderType::Compute, "CSPrefilter",
			g_Environment_CSPrefilter_spirv, sizeof(g_Environment_CSPrefilter_spirv), "Prefilter environment CS");
		if (!m_EquirectangularToCubeShader || !m_DownsampleCubeShader || !m_PrefilterShader)
			return std::unexpected(Error(ErrorCode::DeviceError, "Creating the environment map shaders failed"));

		const auto makeLayout = [this](nvrhi::BindingLayoutItem source, bool sampled)
		{
			nvrhi::BindingLayoutDesc desc;
			desc.setVisibility(nvrhi::ShaderType::Compute).addItem(source);
			if (sampled)
				desc.addItem(nvrhi::BindingLayoutItem::Sampler(0));
			desc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(0))
				.addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(ShaderTypes::EnvironmentConstants)));
			return m_Device->createBindingLayout(desc);
		};
		m_EquirectangularToCubeLayout = makeLayout(nvrhi::BindingLayoutItem::Texture_SRV(0), true);
		m_DownsampleCubeLayout = makeLayout(nvrhi::BindingLayoutItem::Texture_SRV(1), false);
		m_PrefilterLayout = makeLayout(nvrhi::BindingLayoutItem::Texture_SRV(2), true);
		if (!m_EquirectangularToCubeLayout || !m_DownsampleCubeLayout || !m_PrefilterLayout)
			return std::unexpected(
				Error(ErrorCode::DeviceError, "Creating the environment map binding layouts failed"));

		const auto makePipeline = [this](nvrhi::IShader* shader, nvrhi::IBindingLayout* layout)
		{
			return m_Device->createComputePipeline(
				nvrhi::ComputePipelineDesc().setComputeShader(shader).addBindingLayout(layout));
		};
		m_EquirectangularToCube = makePipeline(m_EquirectangularToCubeShader, m_EquirectangularToCubeLayout);
		m_DownsampleCube = makePipeline(m_DownsampleCubeShader, m_DownsampleCubeLayout);
		m_Prefilter = makePipeline(m_PrefilterShader, m_PrefilterLayout);
		if (!m_EquirectangularToCube || !m_DownsampleCube || !m_Prefilter)
			return std::unexpected(Error(ErrorCode::DeviceError, "Creating the environment map pipelines failed"));
		return {};
	}

}
