#pragma once

#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/Error.h"
#include "Lodestone/Graphics/RenderTarget.h"
#include "Lodestone/Renderer/RenderStats.h"

#include <glm/vec2.hpp>
#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace Lodestone {

	// What the scene renderer draws a view into: the final 8-bit image and the scene's depth (an ordinary
	// RenderTarget, which overlays can draw over), and the images and buffers the passes in between use. Each view
	// that's rendered every frame - the editor's viewport, a game's screen - has its own, and keeps its own stats.
	// Created by SceneRenderer::CreateTarget()
	class SceneRenderTarget
	{
	private:
		struct Passkey
		{
			explicit Passkey() = default;
		};

	public:
		// The scene's HDR color and normals
		static constexpr nvrhi::Format HdrFormat = nvrhi::Format::RGBA16_FLOAT;
		static constexpr nvrhi::Format NormalFormat = nvrhi::Format::RGBA16_FLOAT;
		static constexpr nvrhi::Format OcclusionFormat = nvrhi::Format::R8_UNORM;
		// Frames whose GPU timings may be outstanding at once
		static constexpr size_t TimerFrameCount = 4;

		SceneRenderTarget(Passkey passkey, nvrhi::IDevice* device, std::string_view debugName);

		SceneRenderTarget(const SceneRenderTarget&) = delete;
		SceneRenderTarget& operator=(const SceneRenderTarget&) = delete;
		SceneRenderTarget(SceneRenderTarget&&) = delete;
		SceneRenderTarget& operator=(SceneRenderTarget&&) = delete;
		~SceneRenderTarget() = default;

		// Recreates the images at a new size, if it's different. Anything still using the old ones keeps them alive.
		// If it fails, the target keeps its old images
		[[nodiscard]] std::expected<void, Error> Resize(glm::uvec2 size);

		glm::uvec2 GetSize() const { return m_Output->GetSize(); }
		// The final image: 8-bit RGBA, sRGB-encoded, which can be sampled and read back
		nvrhi::ITexture* GetColor() const { return m_Output->GetColor(); }
		// The scene's depth - reversed, 1 at the near plane and 0 at the far plane - which can be sampled
		nvrhi::ITexture* GetDepth() const { return m_Output->GetDepth(); }
		// The final image and the scene's depth, for drawing over the scene
		nvrhi::IFramebuffer* GetFramebuffer() const { return m_Output->GetFramebuffer(); }
		static nvrhi::FramebufferInfo GetFramebufferInfo() { return RenderTarget::GetFramebufferInfo(); }

		// The latest frame's stats
		const RenderStats& GetStats() const { return m_Stats; }

	private:
		friend class SceneRenderer;

		struct TimerFrame
		{
			std::array<nvrhi::TimerQueryHandle, RenderPassCount> Queries;
			bool InFlight = false;
		};

		// Makes the light buffers hold at least these many lights and indices, growing them if they're smaller
		[[nodiscard]] std::expected<void, Error> ReserveLights(size_t lightCount, size_t indexCount);

	private:
		nvrhi::IDevice* m_Device;
		std::string m_DebugName;
		Scope<RenderTarget> m_Output;

		nvrhi::TextureHandle m_Hdr;
		nvrhi::TextureHandle m_Normals;
		nvrhi::TextureHandle m_RawOcclusion;
		nvrhi::TextureHandle m_Occlusion;
		nvrhi::FramebufferHandle m_PrepassFramebuffer;
		nvrhi::FramebufferHandle m_OcclusionFramebuffer;
		nvrhi::FramebufferHandle m_BlurFramebuffer;
		nvrhi::FramebufferHandle m_ForwardFramebuffer;

		// Written every frame. The light buffers grow as needed
		nvrhi::BufferHandle m_FrameConstants;
		nvrhi::BufferHandle m_PunctualLights;
		nvrhi::BufferHandle m_LightIndices;

		// The renderer creates these from the images and buffers above, again whenever they change
		bool m_BindingsValid = false;
		nvrhi::BindingSetHandle m_ViewBindings;
		nvrhi::BindingSetHandle m_OcclusionBindings;
		nvrhi::BindingSetHandle m_BlurBindings;
		nvrhi::BindingSetHandle m_TonemapBindings;

		std::array<TimerFrame, TimerFrameCount> m_TimerFrames;
		size_t m_NextTimerFrame = 0;
		RenderStats m_Stats;
	};

}
