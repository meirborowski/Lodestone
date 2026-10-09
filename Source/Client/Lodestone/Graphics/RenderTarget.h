#pragma once

#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/Error.h"

#include <glm/vec2.hpp>
#include <nvrhi/nvrhi.h>

#include <expected>

namespace Lodestone {

	// An offscreen image to render into: an 8-bit RGBA color texture and a depth texture, with a framebuffer for them.
	// The color texture can also be sampled - to show it in the editor - and read back
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
			nvrhi::IDevice* device, glm::uvec2 size, const char* debugName);

		RenderTarget(Passkey passkey, nvrhi::IDevice* device, const char* debugName);

		// Recreates the textures at a new size, if it's different. Anything still using the old ones keeps them alive
		[[nodiscard]] std::expected<void, Error> Resize(glm::uvec2 size);

		glm::uvec2 GetSize() const { return m_Size; }
		nvrhi::ITexture* GetColor() const { return m_Color; }
		nvrhi::ITexture* GetDepth() const { return m_Depth; }
		nvrhi::IFramebuffer* GetFramebuffer() const { return m_Framebuffer; }
		static nvrhi::FramebufferInfo GetFramebufferInfo();

	private:
		nvrhi::IDevice* m_Device;
		const char* m_DebugName;
		glm::uvec2 m_Size{0};
		nvrhi::TextureHandle m_Color;
		nvrhi::TextureHandle m_Depth;
		nvrhi::FramebufferHandle m_Framebuffer;
	};

}
