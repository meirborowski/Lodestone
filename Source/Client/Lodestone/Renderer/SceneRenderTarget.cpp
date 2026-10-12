#include "Lodestone/Renderer/SceneRenderTarget.h"

#include "Lodestone/Renderer/ShaderTypes.h"

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <bit>

namespace Lodestone {

	namespace {

		// The light buffers start with room for this many, and double as needed
		constexpr size_t MinLightCapacity = 64;
		constexpr size_t MinLightIndexCapacity = 1024;

		nvrhi::TextureHandle CreateImage(
			nvrhi::IDevice* device, glm::uvec2 size, nvrhi::Format format, const char* name)
		{
			nvrhi::TextureDesc desc;
			desc.width = size.x;
			desc.height = size.y;
			desc.format = format;
			desc.debugName = name;
			desc.isRenderTarget = true;
			desc.initialState = nvrhi::ResourceStates::ShaderResource;
			desc.keepInitialState = true;
			return device->createTexture(desc);
		}

		nvrhi::BufferHandle CreateStructuredBuffer(
			nvrhi::IDevice* device, size_t count, uint32_t stride, const char* name)
		{
			nvrhi::BufferDesc desc;
			desc.byteSize = count * stride;
			desc.structStride = stride;
			desc.debugName = name;
			desc.initialState = nvrhi::ResourceStates::ShaderResource;
			desc.keepInitialState = true;
			return device->createBuffer(desc);
		}

	}

	SceneRenderTarget::SceneRenderTarget(Passkey /*passkey*/, nvrhi::IDevice* device, std::string_view debugName)
		: m_Device(device), m_DebugName(debugName)
	{
	}

	std::expected<void, Error> SceneRenderTarget::Resize(glm::uvec2 size)
	{
		if (m_Output && size == m_Output->GetSize())
			return {};
		if (size.x == 0 || size.y == 0)
			return std::unexpected(Error(ErrorCode::InvalidArgument,
				fmt::format("A scene render target can't be {}x{} pixels", size.x, size.y)));

		// Everything is created before anything is replaced, so a target that fails to resize keeps its old images
		auto output = RenderTarget::Create(m_Device, size, m_DebugName);
		if (!output)
			return std::unexpected(output.error());

		const std::string hdrName = fmt::format("{} HDR color", m_DebugName);
		const std::string normalName = fmt::format("{} normals", m_DebugName);
		const std::string rawOcclusionName = fmt::format("{} raw ambient occlusion", m_DebugName);
		const std::string occlusionName = fmt::format("{} ambient occlusion", m_DebugName);
		nvrhi::TextureHandle hdr = CreateImage(m_Device, size, HdrFormat, hdrName.c_str());
		nvrhi::TextureHandle normals = CreateImage(m_Device, size, NormalFormat, normalName.c_str());
		nvrhi::TextureHandle rawOcclusion = CreateImage(m_Device, size, OcclusionFormat, rawOcclusionName.c_str());
		nvrhi::TextureHandle occlusion = CreateImage(m_Device, size, OcclusionFormat, occlusionName.c_str());
		if (!hdr || !normals || !rawOcclusion || !occlusion)
			return std::unexpected(Error(ErrorCode::DeviceError,
				fmt::format("Creating the {}x{} images of '{}' failed", size.x, size.y, m_DebugName)));

		nvrhi::ITexture* depth = (*output)->GetDepth();
		nvrhi::FramebufferHandle prepass =
			m_Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(normals).setDepthAttachment(depth));
		nvrhi::FramebufferHandle occlusionPass =
			m_Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(rawOcclusion));
		nvrhi::FramebufferHandle blur =
			m_Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(occlusion));
		nvrhi::FramebufferHandle forward =
			m_Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(hdr).setDepthAttachment(depth));
		if (!prepass || !occlusionPass || !blur || !forward)
			return std::unexpected(
				Error(ErrorCode::DeviceError, fmt::format("Creating the framebuffers of '{}' failed", m_DebugName)));

		if (!m_FrameConstants)
		{
			const std::string constantsName = fmt::format("{} frame constants", m_DebugName);
			nvrhi::BufferDesc constants;
			constants.byteSize = sizeof(ShaderTypes::FrameConstants);
			constants.debugName = constantsName;
			constants.isConstantBuffer = true;
			constants.initialState = nvrhi::ResourceStates::ConstantBuffer;
			constants.keepInitialState = true;
			m_FrameConstants = m_Device->createBuffer(constants);
			if (!m_FrameConstants)
				return std::unexpected(
					Error(ErrorCode::DeviceError, fmt::format("Creating the constants of '{}' failed", m_DebugName)));
			if (auto reserved = ReserveLights(MinLightCapacity, MinLightIndexCapacity); !reserved)
				return std::unexpected(reserved.error());
		}

		m_Output = std::move(*output);
		m_Hdr = std::move(hdr);
		m_Normals = std::move(normals);
		m_RawOcclusion = std::move(rawOcclusion);
		m_Occlusion = std::move(occlusion);
		m_PrepassFramebuffer = std::move(prepass);
		m_OcclusionFramebuffer = std::move(occlusionPass);
		m_BlurFramebuffer = std::move(blur);
		m_ForwardFramebuffer = std::move(forward);
		m_BindingsValid = false;
		return {};
	}

	std::expected<void, Error> SceneRenderTarget::ReserveLights(size_t lightCount, size_t indexCount)
	{
		const auto capacity = [](const nvrhi::BufferHandle& buffer, uint32_t stride)
		{ return buffer ? static_cast<size_t>(buffer->getDesc().byteSize) / stride : 0; };
		constexpr auto lightStride = static_cast<uint32_t>(sizeof(ShaderTypes::PunctualLight));
		constexpr auto indexStride = static_cast<uint32_t>(sizeof(uint32_t));

		if (capacity(m_PunctualLights, lightStride) < lightCount)
		{
			const std::string name = fmt::format("{} punctual lights", m_DebugName);
			nvrhi::BufferHandle buffer = CreateStructuredBuffer(
				m_Device, std::bit_ceil(std::max(lightCount, MinLightCapacity)), lightStride, name.c_str());
			if (!buffer)
				return std::unexpected(Error(
					ErrorCode::DeviceError, fmt::format("Creating the light buffer of '{}' failed", m_DebugName)));
			m_PunctualLights = std::move(buffer);
			m_BindingsValid = false;
		}
		if (capacity(m_LightIndices, indexStride) < indexCount)
		{
			const std::string name = fmt::format("{} light indices", m_DebugName);
			nvrhi::BufferHandle buffer = CreateStructuredBuffer(
				m_Device, std::bit_ceil(std::max(indexCount, MinLightIndexCapacity)), indexStride, name.c_str());
			if (!buffer)
				return std::unexpected(Error(ErrorCode::DeviceError,
					fmt::format("Creating the light index buffer of '{}' failed", m_DebugName)));
			m_LightIndices = std::move(buffer);
			m_BindingsValid = false;
		}
		return {};
	}

}
