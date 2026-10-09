#include "Lodestone/Graphics/RenderTarget.h"

#include <spdlog/fmt/fmt.h>

#include <string>

namespace Lodestone {

	std::expected<Scope<RenderTarget>, Error> RenderTarget::Create(
		nvrhi::IDevice* device, glm::uvec2 size, const char* debugName)
	{
		auto target = CreateScope<RenderTarget>(Passkey(), device, debugName);
		if (auto resized = target->Resize(size); !resized)
			return std::unexpected(resized.error());
		return target;
	}

	RenderTarget::RenderTarget(Passkey /*passkey*/, nvrhi::IDevice* device, const char* debugName)
		: m_Device(device), m_DebugName(debugName)
	{
	}

	std::expected<void, Error> RenderTarget::Resize(glm::uvec2 size)
	{
		if (size == m_Size && m_Framebuffer)
			return {};
		if (size.x == 0 || size.y == 0)
			return std::unexpected(Error(
				ErrorCode::InvalidArgument, fmt::format("A render target can't be {}x{} pixels", size.x, size.y)));

		const std::string colorName = fmt::format("{} color", m_DebugName);
		nvrhi::TextureDesc color;
		color.width = size.x;
		color.height = size.y;
		color.format = ColorFormat;
		color.debugName = colorName;
		color.isRenderTarget = true;
		color.isShaderResource = true;
		color.initialState = nvrhi::ResourceStates::ShaderResource;
		color.keepInitialState = true;
		nvrhi::TextureHandle colorTexture = m_Device->createTexture(color);

		const std::string depthName = fmt::format("{} depth", m_DebugName);
		nvrhi::TextureDesc depth;
		depth.width = size.x;
		depth.height = size.y;
		depth.format = DepthFormat;
		depth.debugName = depthName;
		depth.isRenderTarget = true;
		depth.initialState = nvrhi::ResourceStates::DepthWrite;
		depth.keepInitialState = true;
		nvrhi::TextureHandle depthTexture = m_Device->createTexture(depth);

		if (!colorTexture || !depthTexture)
			return std::unexpected(Error(ErrorCode::DeviceError,
				fmt::format("Creating the {}x{} render target '{}' failed", size.x, size.y, m_DebugName)));
		nvrhi::FramebufferHandle framebuffer = m_Device->createFramebuffer(
			nvrhi::FramebufferDesc().addColorAttachment(colorTexture).setDepthAttachment(depthTexture));
		if (!framebuffer)
			return std::unexpected(
				Error(ErrorCode::DeviceError, fmt::format("Creating the framebuffer of '{}' failed", m_DebugName)));

		m_Color = std::move(colorTexture);
		m_Depth = std::move(depthTexture);
		m_Framebuffer = std::move(framebuffer);
		m_Size = size;
		return {};
	}

	nvrhi::FramebufferInfo RenderTarget::GetFramebufferInfo()
	{
		return nvrhi::FramebufferInfo().addColorFormat(ColorFormat).setDepthFormat(DepthFormat);
	}

}
