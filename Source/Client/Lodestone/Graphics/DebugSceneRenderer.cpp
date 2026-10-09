#include "Lodestone/Graphics/DebugSceneRenderer.h"

#include "Lodestone/Graphics/RenderTarget.h"

#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/matrix.hpp>
#include <nvrhi/utils.h>

#include <cstdint>

namespace Lodestone {

	namespace {

		// SPIR-V compiled from Shaders/DebugCube.hlsl and Shaders/DebugGrid.hlsl at build time
#include "Shaders/DebugCube_PSMain.spirv.h"
#include "Shaders/DebugCube_VSMain.spirv.h"
#include "Shaders/DebugGrid_PSMain.spirv.h"
#include "Shaders/DebugGrid_VSMain.spirv.h"

		// Matches CubeConstants in DebugCube.hlsl
		struct CubeConstants
		{
			glm::mat4 WorldViewProjection;
			glm::vec4 Color;
			glm::vec4 LightDirection;
		};
		static_assert(sizeof(CubeConstants) <= 128, "Push constants beyond 128 bytes aren't guaranteed");

		// Matches GridConstants in DebugGrid.hlsl
		struct GridConstants
		{
			glm::mat4 ViewProjection;
			glm::vec4 Color;
			glm::vec4 AxisColorX;
			glm::vec4 AxisColorZ;
			float Spacing = 1.0f;
			uint32_t HalfLineCount = 0;
			uint32_t Padding[2] = {};
		};
		static_assert(sizeof(GridConstants) <= 128, "Push constants beyond 128 bytes aren't guaranteed");

		constexpr uint32_t GridHalfLineCount = 20;
		constexpr glm::vec3 LightDirection{-0.4f, -1.0f, -0.6f};

		nvrhi::ShaderHandle CreateShader(
			nvrhi::IDevice* device, nvrhi::ShaderType type, const char* name, const uint8_t* code, size_t size)
		{
			return device->createShader(nvrhi::ShaderDesc()
											.setShaderType(type)
											.setEntryName(type == nvrhi::ShaderType::Vertex ? "VSMain" : "PSMain")
											.setDebugName(name),
				code, size);
		}

	}

	std::expected<Scope<DebugSceneRenderer>, Error> DebugSceneRenderer::Create(nvrhi::IDevice* device)
	{
		auto renderer = CreateScope<DebugSceneRenderer>(Passkey());
		renderer->m_CubeVertexShader = CreateShader(device, nvrhi::ShaderType::Vertex, "Debug cube VS",
			g_DebugCube_VSMain_spirv, sizeof(g_DebugCube_VSMain_spirv));
		renderer->m_CubePixelShader = CreateShader(device, nvrhi::ShaderType::Pixel, "Debug cube PS",
			g_DebugCube_PSMain_spirv, sizeof(g_DebugCube_PSMain_spirv));
		renderer->m_GridVertexShader = CreateShader(device, nvrhi::ShaderType::Vertex, "Debug grid VS",
			g_DebugGrid_VSMain_spirv, sizeof(g_DebugGrid_VSMain_spirv));
		renderer->m_GridPixelShader = CreateShader(device, nvrhi::ShaderType::Pixel, "Debug grid PS",
			g_DebugGrid_PSMain_spirv, sizeof(g_DebugGrid_PSMain_spirv));
		if (!renderer->m_CubeVertexShader || !renderer->m_CubePixelShader || !renderer->m_GridVertexShader ||
			!renderer->m_GridPixelShader)
			return std::unexpected(Error(ErrorCode::DeviceError, "Creating the debug scene shaders failed"));

		const auto makeLayout = [device](uint32_t size)
		{
			return device->createBindingLayout(nvrhi::BindingLayoutDesc()
					.setVisibility(nvrhi::ShaderType::All)
					.addItem(nvrhi::BindingLayoutItem::PushConstants(0, size)));
		};
		renderer->m_CubeLayout = makeLayout(sizeof(CubeConstants));
		renderer->m_GridLayout = makeLayout(sizeof(GridConstants));
		if (!renderer->m_CubeLayout || !renderer->m_GridLayout)
			return std::unexpected(Error(ErrorCode::DeviceError, "Creating the debug scene binding layouts failed"));
		renderer->m_CubeBindings = device->createBindingSet(
			nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(CubeConstants))),
			renderer->m_CubeLayout);
		renderer->m_GridBindings = device->createBindingSet(
			nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(GridConstants))),
			renderer->m_GridLayout);
		if (!renderer->m_CubeBindings || !renderer->m_GridBindings)
			return std::unexpected(Error(ErrorCode::DeviceError, "Creating the debug scene binding sets failed"));

		const nvrhi::FramebufferInfo framebufferInfo = RenderTarget::GetFramebufferInfo();

		nvrhi::GraphicsPipelineDesc cube;
		cube.setVertexShader(renderer->m_CubeVertexShader)
			.setPixelShader(renderer->m_CubePixelShader)
			.addBindingLayout(renderer->m_CubeLayout);
		// Cubes are convex and depth tested, so their back faces never show and needn't be culled
		cube.renderState.rasterState.setCullNone();
		cube.renderState.depthStencilState.enableDepthTest().enableDepthWrite().setDepthFunc(
			nvrhi::ComparisonFunc::LessOrEqual);
		renderer->m_CubePipeline = device->createGraphicsPipeline(cube, framebufferInfo);

		nvrhi::GraphicsPipelineDesc grid;
		grid.setVertexShader(renderer->m_GridVertexShader)
			.setPixelShader(renderer->m_GridPixelShader)
			.addBindingLayout(renderer->m_GridLayout)
			.setPrimType(nvrhi::PrimitiveType::LineList);
		grid.renderState.rasterState.setCullNone();
		grid.renderState.depthStencilState.enableDepthTest().enableDepthWrite().setDepthFunc(
			nvrhi::ComparisonFunc::LessOrEqual);
		renderer->m_GridPipeline = device->createGraphicsPipeline(grid, framebufferInfo);

		if (!renderer->m_CubePipeline || !renderer->m_GridPipeline)
			return std::unexpected(Error(ErrorCode::DeviceError, "Creating the debug scene pipelines failed"));
		return renderer;
	}

	DebugSceneRenderer::DebugSceneRenderer(Passkey /*passkey*/)
	{
	}

	void DebugSceneRenderer::Render(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* framebuffer,
		const DebugView& view, std::span<const DebugCube> cubes) const
	{
		const glm::vec4& clear = view.ClearColor;
		nvrhi::utils::ClearColorAttachment(
			commandList, framebuffer, 0, nvrhi::Color(clear.r, clear.g, clear.b, clear.a));
		commandList->clearDepthStencilTexture(
			framebuffer->getDesc().depthAttachment.texture, nvrhi::AllSubresources, true, 1.0f, false, 0);

		const nvrhi::ViewportState viewport =
			nvrhi::ViewportState().addViewportAndScissorRect(framebuffer->getFramebufferInfo().getViewport());

		if (view.ShowGrid)
		{
			commandList->setGraphicsState(nvrhi::GraphicsState()
					.setPipeline(m_GridPipeline)
					.setFramebuffer(framebuffer)
					.setViewport(viewport)
					.addBindingSet(m_GridBindings));
			const GridConstants constants{
				.ViewProjection = view.ViewProjection,
				.Color = glm::vec4(0.35f, 0.35f, 0.38f, 1.0f),
				.AxisColorX = glm::vec4(0.75f, 0.25f, 0.25f, 1.0f),
				.AxisColorZ = glm::vec4(0.25f, 0.4f, 0.8f, 1.0f),
				.Spacing = 1.0f,
				.HalfLineCount = GridHalfLineCount,
			};
			commandList->setPushConstants(&constants, sizeof(constants));
			commandList->draw(nvrhi::DrawArguments().setVertexCount((GridHalfLineCount * 2 + 1) * 4));
		}

		if (cubes.empty())
			return;
		commandList->setGraphicsState(nvrhi::GraphicsState()
				.setPipeline(m_CubePipeline)
				.setFramebuffer(framebuffer)
				.setViewport(viewport)
				.addBindingSet(m_CubeBindings));
		for (const DebugCube& cube : cubes)
		{
			// The light's direction in the cube's space: the inverse of its rotation and scale, applied to the
			// direction. Degenerate (zero-scale) cubes get the world direction, which only affects their shading
			const glm::mat3 linear(cube.World);
			const glm::vec3 local =
				glm::determinant(linear) != 0.0f ? glm::inverse(linear) * LightDirection : LightDirection;
			const CubeConstants constants{
				.WorldViewProjection = view.ViewProjection * cube.World,
				.Color = cube.Color,
				.LightDirection = glm::vec4(local, 0.0f),
			};
			commandList->setPushConstants(&constants, sizeof(constants));
			commandList->draw(nvrhi::DrawArguments().setVertexCount(36));
		}
	}

}
