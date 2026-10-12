#pragma once

#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/Error.h"

#include <glm/vec2.hpp>
#include <nvrhi/nvrhi.h>

#include <expected>
#include <string>
#include <string_view>

namespace Lodestone {

	// An offscreen image to render into: an 8-bit RGBA color texture and a depth texture, with a framebuffer for them.
	// Both can also be sampled - the color to show it in the editor, the depth for screen-space effects - and the color
	// read back
	class RenderTarget
	{
	private:
		struct Passkey
		{
			explicit Passkey() = default;
		};

	public:
		static constexpr nvrhi::Format ColorFormat = nvrhi::Format::RGBA8_UNORM;
		static constexpr nvrhi::Format DepthFormat = nvrhi::Format::D32;

		[[nodiscard]] static std::expected<Scope<RenderTarget>, Error> Create(
			nvrhi::IDevice* device, glm::uvec2 size, std::string_view debugName);

		RenderTarget(Passkey passkey, nvrhi::IDevice* device, std::string_view debugName);

		RenderTarget(const RenderTarget&) = delete;
		RenderTarget& operator=(const RenderTarget&) = delete;
		RenderTarget(RenderTarget&&) = delete;
		RenderTarget& operator=(RenderTarget&&) = delete;
		~RenderTarget() = default;

		// Recreates the textures at a new size, if it's different. Anything still using the old ones keeps them alive
		[[nodiscard]] std::expected<void, Error> Resize(glm::uvec2 size);

		glm::uvec2 GetSize() const { return m_Size; }
		nvrhi::ITexture* GetColor() const { return m_Color; }
		nvrhi::ITexture* GetDepth() const { return m_Depth; }
		nvrhi::IFramebuffer* GetFramebuffer() const { return m_Framebuffer; }
		static nvrhi::FramebufferInfo GetFramebufferInfo();

	private:
		nvrhi::IDevice* m_Device;
		std::string m_DebugName;
		glm::uvec2 m_Size{0};
		nvrhi::TextureHandle m_Color;
		nvrhi::TextureHandle m_Depth;
		nvrhi::FramebufferHandle m_Framebuffer;
	};

}
