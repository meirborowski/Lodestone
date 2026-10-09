#include "Lodestone/Graphics/TriangleRenderer.h"

#include <nvrhi/utils.h>

#include <cstdint>

namespace Lodestone {

	namespace {

		// SPIR-V compiled from Shaders/Triangle.hlsl at build time
#include "Shaders/Triangle_PSMain.spirv.h"
#include "Shaders/Triangle_VSMain.spirv.h"

	}

	std::expected<Scope<TriangleRenderer>, Error> TriangleRenderer::Create(
		nvrhi::IDevice* device, const nvrhi::FramebufferInfo& framebufferInfo)
	{
		auto renderer = CreateScope<TriangleRenderer>(Passkey());

		renderer->m_VertexShader = device->createShader(nvrhi::ShaderDesc()
															.setShaderType(nvrhi::ShaderType::Vertex)
															.setEntryName("VSMain")
															.setDebugName("Triangle VS"),
			g_Triangle_VSMain_spirv, sizeof(g_Triangle_VSMain_spirv));
		renderer->m_PixelShader = device->createShader(nvrhi::ShaderDesc()
														   .setShaderType(nvrhi::ShaderType::Pixel)
														   .setEntryName("PSMain")
														   .setDebugName("Triangle PS"),
			g_Triangle_PSMain_spirv, sizeof(g_Triangle_PSMain_spirv));
		if (!renderer->m_VertexShader || !renderer->m_PixelShader)
			return std::unexpected(Error(ErrorCode::DeviceError, "Creating the triangle shaders failed"));

		nvrhi::GraphicsPipelineDesc pipelineDesc;
		pipelineDesc.setVertexShader(renderer->m_VertexShader).setPixelShader(renderer->m_PixelShader);
		pipelineDesc.renderState.rasterState.setCullNone();
		pipelineDesc.renderState.depthStencilState.disableDepthTest().disableDepthWrite();
		renderer->m_Pipeline = device->createGraphicsPipeline(pipelineDesc, framebufferInfo);
		if (!renderer->m_Pipeline)
			return std::unexpected(Error(ErrorCode::DeviceError, "Creating the triangle pipeline failed"));

		return renderer;
	}

	TriangleRenderer::TriangleRenderer(Passkey /*passkey*/)
	{
	}

	void TriangleRenderer::Render(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* framebuffer) const
	{
		nvrhi::utils::ClearColorAttachment(commandList, framebuffer, 0, nvrhi::Color(0.1f, 0.1f, 0.12f, 1.0f));

		const nvrhi::FramebufferInfoEx& info = framebuffer->getFramebufferInfo();
		nvrhi::GraphicsState state;
		state.setPipeline(m_Pipeline)
			.setFramebuffer(framebuffer)
			.setViewport(nvrhi::ViewportState().addViewportAndScissorRect(info.getViewport()));
		commandList->setGraphicsState(state);
		commandList->draw(nvrhi::DrawArguments().setVertexCount(3));
	}

}
