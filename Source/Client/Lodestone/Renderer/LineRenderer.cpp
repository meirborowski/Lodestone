#include "Lodestone/Renderer/LineRenderer.h"

#include "Lodestone/Core/Log.h"
#include "Lodestone/Graphics/RenderTarget.h"
#include "Lodestone/Graphics/ShaderModule.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>

namespace Lodestone {

	namespace {

		// SPIR-V compiled from Shaders/Lines.hlsl at build time
#include "Shaders/Lines_PSMain.spirv.h"
#include "Shaders/Lines_VSMain.spirv.h"

		// Matches LineConstants in Lines.hlsl
		struct LineConstants
		{
			glm::mat4 ViewProjection{1.0f};
		};

		static_assert(sizeof(LineVertex) == 16);
		constexpr size_t MinVertexCapacity = 1024;

	}

	std::expected<Scope<LineRenderer>, Error> LineRenderer::Create(nvrhi::IDevice* device)
	{
		auto renderer = CreateScope<LineRenderer>(Passkey(), device);
		renderer->m_VertexShader = CreateShaderModule(device, nvrhi::ShaderType::Vertex, "VSMain", g_Lines_VSMain_spirv,
			sizeof(g_Lines_VSMain_spirv), "Lines VS");
		renderer->m_PixelShader = CreateShaderModule(
			device, nvrhi::ShaderType::Pixel, "PSMain", g_Lines_PSMain_spirv, sizeof(g_Lines_PSMain_spirv), "Lines PS");
		if (!renderer->m_VertexShader || !renderer->m_PixelShader)
			return std::unexpected(Error(ErrorCode::DeviceError, "Creating the line shaders failed"));

		const std::array<nvrhi::VertexAttributeDesc, 2> attributes = {
			nvrhi::VertexAttributeDesc()
				.setName("POSITION")
				.setFormat(nvrhi::Format::RGB32_FLOAT)
				.setOffset(offsetof(LineVertex, Position))
				.setElementStride(sizeof(LineVertex)),
			nvrhi::VertexAttributeDesc()
				.setName("COLOR")
				.setFormat(nvrhi::Format::RGBA8_UNORM)
				.setOffset(offsetof(LineVertex, Color))
				.setElementStride(sizeof(LineVertex)),
		};
		renderer->m_InputLayout = device->createInputLayout(
			attributes.data(), static_cast<uint32_t>(attributes.size()), renderer->m_VertexShader);
		renderer->m_Layout = device->createBindingLayout(nvrhi::BindingLayoutDesc()
				.setVisibility(nvrhi::ShaderType::Vertex)
				.addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(LineConstants))));
		if (!renderer->m_InputLayout || !renderer->m_Layout)
			return std::unexpected(Error(ErrorCode::DeviceError, "Creating the line layouts failed"));
		renderer->m_Bindings = device->createBindingSet(
			nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(LineConstants))),
			renderer->m_Layout);

		const auto makePipeline = [&](bool depthTested)
		{
			nvrhi::GraphicsPipelineDesc desc;
			desc.setVertexShader(renderer->m_VertexShader)
				.setPixelShader(renderer->m_PixelShader)
				.setInputLayout(renderer->m_InputLayout)
				.addBindingLayout(renderer->m_Layout)
				.setPrimType(nvrhi::PrimitiveType::LineList);
			desc.renderState.rasterState.setCullNone();
			desc.renderState.blendState.targets[0]
				.setBlendEnable(true)
				.setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
				.setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
				.setSrcBlendAlpha(nvrhi::BlendFactor::One)
				.setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
			desc.renderState.depthStencilState.disableDepthWrite();
			if (depthTested)
				desc.renderState.depthStencilState.enableDepthTest().setDepthFunc(
					nvrhi::ComparisonFunc::GreaterOrEqual);
			else
				desc.renderState.depthStencilState.disableDepthTest();
			return device->createGraphicsPipeline(desc, RenderTarget::GetFramebufferInfo());
		};
		renderer->m_DepthTestedPipeline = makePipeline(true);
		renderer->m_OnTopPipeline = makePipeline(false);
		if (!renderer->m_Bindings || !renderer->m_DepthTestedPipeline || !renderer->m_OnTopPipeline)
			return std::unexpected(Error(ErrorCode::DeviceError, "Creating the line pipelines failed"));
		return renderer;
	}

	LineRenderer::LineRenderer(Passkey /*passkey*/, nvrhi::IDevice* device)
		: m_Device(device)
	{
	}

	void LineRenderer::Render(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* framebuffer,
		const glm::mat4& viewProjection, std::span<const LineVertex> depthTested, std::span<const LineVertex> onTop)
	{
		// Whole segments only
		depthTested = depthTested.first(depthTested.size() & ~size_t(1));
		onTop = onTop.first(onTop.size() & ~size_t(1));
		const size_t count = depthTested.size() + onTop.size();
		if (count == 0)
			return;

		const size_t capacity = m_Vertices ? m_Vertices->getDesc().byteSize / sizeof(LineVertex) : 0;
		if (capacity < count)
		{
			nvrhi::BufferDesc desc;
			desc.byteSize = std::bit_ceil(std::max(count, MinVertexCapacity)) * sizeof(LineVertex);
			desc.debugName = "Line vertices";
			desc.isVertexBuffer = true;
			desc.initialState = nvrhi::ResourceStates::VertexBuffer;
			desc.keepInitialState = true;
			nvrhi::BufferHandle buffer = m_Device->createBuffer(desc);
			if (!buffer)
			{
				LS_CORE_ERROR("Creating a buffer for {} line vertices failed, so they aren't drawn", count);
				return;
			}
			m_Vertices = std::move(buffer);
		}
		m_Staging.assign(depthTested.begin(), depthTested.end());
		m_Staging.insert(m_Staging.end(), onTop.begin(), onTop.end());
		commandList->writeBuffer(m_Vertices, m_Staging.data(), m_Staging.size() * sizeof(LineVertex));

		const nvrhi::ViewportState viewport =
			nvrhi::ViewportState().addViewportAndScissorRect(framebuffer->getFramebufferInfo().getViewport());
		const LineConstants constants{.ViewProjection = viewProjection};
		const auto draw = [&](nvrhi::IGraphicsPipeline* pipeline, size_t first, size_t vertexCount)
		{
			if (vertexCount == 0)
				return;
			commandList->setGraphicsState(nvrhi::GraphicsState()
					.setPipeline(pipeline)
					.setFramebuffer(framebuffer)
					.setViewport(viewport)
					.addBindingSet(m_Bindings)
					.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(m_Vertices).setSlot(0)));
			commandList->setPushConstants(&constants, sizeof(constants));
			commandList->draw(nvrhi::DrawArguments()
					.setVertexCount(static_cast<uint32_t>(vertexCount))
					.setStartVertexLocation(static_cast<uint32_t>(first)));
		};
		draw(m_DepthTestedPipeline, 0, depthTested.size());
		draw(m_OnTopPipeline, depthTested.size(), onTop.size());
	}

}
