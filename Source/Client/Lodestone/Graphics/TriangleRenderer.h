#pragma once

#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/Error.h"

#include <nvrhi/nvrhi.h>

#include <expected>

namespace Lodestone {

	// Clears a framebuffer and draws a triangle with a red, a green and a blue corner. It exercises the whole rendering
	// backend - shader compilation, pipelines and drawing - and is the subject of the first reference-image test. The
	// scene renderer replaces it in the runtime in Milestone 5
	class TriangleRenderer
	{
	private:
		struct Passkey
		{
			explicit Passkey() = default;
		};

	public:
		// The renderer draws into framebuffers that match framebufferInfo
		[[nodiscard]] static std::expected<Scope<TriangleRenderer>, Error> Create(
			nvrhi::IDevice* device, const nvrhi::FramebufferInfo& framebufferInfo);

		explicit TriangleRenderer(Passkey passkey);

		// Records the clear and the draw into an open command list
		void Render(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* framebuffer) const;

	private:
		nvrhi::ShaderHandle m_VertexShader;
		nvrhi::ShaderHandle m_PixelShader;
		nvrhi::GraphicsPipelineHandle m_Pipeline;
	};

}
