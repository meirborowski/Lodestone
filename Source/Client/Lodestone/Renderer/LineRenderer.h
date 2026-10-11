#pragma once

#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/Error.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace Lodestone {

	struct LineVertex
	{
		glm::vec3 Position{0.0f};
		// RGBA, 8 bits each, red in the lowest byte, sRGB-encoded like the image the lines are drawn on (MakeColor())
		uint32_t Color = 0xFFFFFFFF;

		// A color from 0-255 channels
		static constexpr uint32_t MakeColor(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255)
		{
			return static_cast<uint32_t>(red) | (static_cast<uint32_t>(green) << 8) |
				(static_cast<uint32_t>(blue) << 16) | (static_cast<uint32_t>(alpha) << 24);
		}
	};

	// Draws colored line segments over a rendered scene - the editor's overlays, debug drawing - into framebuffers like
	// SceneRenderTarget's (8-bit color and the scene's reversed depth). Lines blend by their alpha
	class LineRenderer
	{
	private:
		struct Passkey
		{
			explicit Passkey() = default;
		};

	public:
		[[nodiscard]] static std::expected<Scope<LineRenderer>, Error> Create(nvrhi::IDevice* device);

		LineRenderer(Passkey passkey, nvrhi::IDevice* device);

		// Records the lines into an open command list: each pair of vertices is a segment. Depth-tested lines hide
		// behind what the scene drew; the others draw on top of everything
		void Render(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* framebuffer, const glm::mat4& viewProjection,
			std::span<const LineVertex> depthTested, std::span<const LineVertex> onTop);

	private:
		nvrhi::IDevice* m_Device;
		nvrhi::ShaderHandle m_VertexShader;
		nvrhi::ShaderHandle m_PixelShader;
		nvrhi::InputLayoutHandle m_InputLayout;
		nvrhi::BindingLayoutHandle m_Layout;
		nvrhi::BindingSetHandle m_Bindings;
		nvrhi::GraphicsPipelineHandle m_DepthTestedPipeline;
		nvrhi::GraphicsPipelineHandle m_OnTopPipeline;
		// Grows as needed
		nvrhi::BufferHandle m_Vertices;
		std::vector<LineVertex> m_Staging;
	};

}
