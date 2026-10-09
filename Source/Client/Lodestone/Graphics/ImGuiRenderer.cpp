#include "Lodestone/Graphics/ImGuiRenderer.h"

#include "Lodestone/Core/Assert.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/mat4x4.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace Lodestone {

	namespace {

		// SPIR-V compiled from Shaders/ImGui.hlsl at build time
#include "Shaders/ImGui_PSMain.spirv.h"
#include "Shaders/ImGui_VSMain.spirv.h"

		constexpr const char* BackendName = "Lodestone NVRHI";

		ImTextureID ToTextureId(nvrhi::ITexture* texture)
		{
			return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(texture));
		}

		nvrhi::ITexture* FromTextureId(ImTextureID id)
		{
			// Dear ImGui's texture IDs are integers; ours hold texture pointers
			return reinterpret_cast<nvrhi::ITexture*>(static_cast<uintptr_t>(id)); // NOLINT(performance-no-int-to-ptr)
		}

		// Dear ImGui's request to reset the render state between draws. There's nothing to do: every draw sets its
		// whole state
		void ResetRenderState(const ImDrawList* /*drawList*/, const ImDrawCmd* /*command*/)
		{
		}

	}

	std::expected<Scope<ImGuiRenderer>, Error> ImGuiRenderer::Create(
		nvrhi::IDevice* device, const nvrhi::FramebufferInfo& framebufferInfo)
	{
		ImGuiIO& io = ImGui::GetIO();
		LS_CORE_ASSERT(io.BackendRendererUserData == nullptr, "ImGui already has a renderer");

		auto renderer = CreateScope<ImGuiRenderer>(Passkey(), device);
		renderer->m_VertexShader = device->createShader(nvrhi::ShaderDesc()
															.setShaderType(nvrhi::ShaderType::Vertex)
															.setEntryName("VSMain")
															.setDebugName("ImGui VS"),
			g_ImGui_VSMain_spirv, sizeof(g_ImGui_VSMain_spirv));
		renderer->m_PixelShader = device->createShader(
			nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Pixel).setEntryName("PSMain").setDebugName("ImGui PS"),
			g_ImGui_PSMain_spirv, sizeof(g_ImGui_PSMain_spirv));
		if (!renderer->m_VertexShader || !renderer->m_PixelShader)
			return std::unexpected(Error(ErrorCode::DeviceError, "Creating the ImGui shaders failed"));

		// In the order of VertexInput in ImGui.hlsl, which is how Vulkan matches them
		const std::array attributes = {
			nvrhi::VertexAttributeDesc()
				.setName("POSITION")
				.setFormat(nvrhi::Format::RG32_FLOAT)
				.setOffset(offsetof(ImDrawVert, pos))
				.setElementStride(sizeof(ImDrawVert)),
			nvrhi::VertexAttributeDesc()
				.setName("TEXCOORD")
				.setFormat(nvrhi::Format::RG32_FLOAT)
				.setOffset(offsetof(ImDrawVert, uv))
				.setElementStride(sizeof(ImDrawVert)),
			nvrhi::VertexAttributeDesc()
				.setName("COLOR")
				.setFormat(nvrhi::Format::RGBA8_UNORM)
				.setOffset(offsetof(ImDrawVert, col))
				.setElementStride(sizeof(ImDrawVert)),
		};
		renderer->m_InputLayout = device->createInputLayout(
			attributes.data(), static_cast<uint32_t>(attributes.size()), renderer->m_VertexShader);

		renderer->m_BindingLayout = device->createBindingLayout(nvrhi::BindingLayoutDesc()
				.setVisibility(nvrhi::ShaderType::All)
				.addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(glm::mat4)))
				.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
				.addItem(nvrhi::BindingLayoutItem::Sampler(0)));
		renderer->m_Sampler = device->createSampler(
			nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Wrap));
		if (!renderer->m_InputLayout || !renderer->m_BindingLayout || !renderer->m_Sampler)
			return std::unexpected(Error(ErrorCode::DeviceError, "Creating the ImGui pipeline resources failed"));

		nvrhi::GraphicsPipelineDesc pipeline;
		pipeline.setVertexShader(renderer->m_VertexShader)
			.setPixelShader(renderer->m_PixelShader)
			.setInputLayout(renderer->m_InputLayout)
			.addBindingLayout(renderer->m_BindingLayout);
		pipeline.renderState.rasterState.setCullNone().enableScissor();
		pipeline.renderState.depthStencilState.disableDepthTest().disableDepthWrite();
		pipeline.renderState.blendState.targets[0]
			.enableBlend()
			.setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
			.setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
			.setSrcBlendAlpha(nvrhi::BlendFactor::One)
			.setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
		renderer->m_Pipeline = device->createGraphicsPipeline(pipeline, framebufferInfo);
		if (!renderer->m_Pipeline)
			return std::unexpected(Error(ErrorCode::DeviceError, "Creating the ImGui pipeline failed"));

		io.BackendRendererName = BackendName;
		io.BackendRendererUserData = renderer.get();
		ImGui::GetPlatformIO().DrawCallback_ResetRenderState = &ResetRenderState;
		// Large meshes can index past 64K vertices; Dear ImGui creates and updates textures as it needs them
		io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
		return renderer;
	}

	ImGuiRenderer::ImGuiRenderer(Passkey /*passkey*/, nvrhi::IDevice* device)
		: m_Device(device)
	{
	}

	ImGuiRenderer::~ImGuiRenderer()
	{
		for (ImTextureData* texture : ImGui::GetPlatformIO().Textures)
		{
			if (texture->BackendUserData != nullptr)
				DestroyTexture(*texture);
		}
		ImGui::GetPlatformIO().DrawCallback_ResetRenderState = nullptr;
		ImGuiIO& io = ImGui::GetIO();
		io.BackendRendererName = nullptr;
		io.BackendRendererUserData = nullptr;
		io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
	}

	ImTextureID ImGuiRenderer::GetTextureId(nvrhi::ITexture* texture)
	{
		m_FrameTextures.emplace_back(texture);
		return ToTextureId(texture);
	}

	void ImGuiRenderer::Render(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* framebuffer, ImDrawData* drawData)
	{
		m_BindingSets.clear();

		// Textures are created and updated even for frames that draw nothing
		if (drawData->Textures != nullptr)
		{
			for (ImTextureData* texture : *drawData->Textures)
			{
				if (texture->Status != ImTextureStatus_OK)
					UpdateTexture(commandList, *texture);
			}
		}

		if (drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f || drawData->TotalVtxCount == 0)
		{
			m_FrameTextures.clear();
			return;
		}

		EnsureBufferSizes(drawData->TotalVtxCount, drawData->TotalIdxCount);
		size_t vertexOffset = 0;
		size_t indexOffset = 0;
		for (const ImDrawList* drawList : drawData->CmdLists)
		{
			const size_t vertexBytes = static_cast<size_t>(drawList->VtxBuffer.Size) * sizeof(ImDrawVert);
			const size_t indexBytes = static_cast<size_t>(drawList->IdxBuffer.Size) * sizeof(ImDrawIdx);
			commandList->writeBuffer(m_VertexBuffer, drawList->VtxBuffer.Data, vertexBytes, vertexOffset);
			commandList->writeBuffer(m_IndexBuffer, drawList->IdxBuffer.Data, indexBytes, indexOffset);
			vertexOffset += vertexBytes;
			indexOffset += indexBytes;
		}

		// ImGui's display coordinates have a top-left origin; NVRHI's clip space has +Y up
		const ImVec2 position = drawData->DisplayPos;
		const ImVec2 size = drawData->DisplaySize;
		const glm::mat4 projection =
			glm::orthoRH_ZO(position.x, position.x + size.x, position.y + size.y, position.y, -1.0f, 1.0f);

		const nvrhi::FramebufferInfoEx& info = framebuffer->getFramebufferInfo();
		const ImVec2 scale = drawData->FramebufferScale;
		uint32_t globalVertexOffset = 0;
		uint32_t globalIndexOffset = 0;
		for (const ImDrawList* drawList : drawData->CmdLists)
		{
			for (const ImDrawCmd& command : drawList->CmdBuffer)
			{
				if (command.UserCallback != nullptr)
				{
					if (command.UserCallback != &ResetRenderState)
						command.UserCallback(drawList, &command);
					continue;
				}

				// The clip rectangle in framebuffer pixels, inside the framebuffer
				const float left = std::max((command.ClipRect.x - position.x) * scale.x, 0.0f);
				const float top = std::max((command.ClipRect.y - position.y) * scale.y, 0.0f);
				const float right =
					std::min((command.ClipRect.z - position.x) * scale.x, static_cast<float>(info.width));
				const float bottom =
					std::min((command.ClipRect.w - position.y) * scale.y, static_cast<float>(info.height));
				if (right <= left || bottom <= top)
					continue;

				nvrhi::IBindingSet* bindings = GetBindingSet(FromTextureId(command.GetTexID()));
				if (bindings == nullptr)
					continue;

				nvrhi::ViewportState viewport;
				viewport.addViewport(info.getViewport());
				viewport.addScissorRect(nvrhi::Rect(
					static_cast<int>(left), static_cast<int>(right), static_cast<int>(top), static_cast<int>(bottom)));
				commandList->setGraphicsState(nvrhi::GraphicsState()
						.setPipeline(m_Pipeline)
						.setFramebuffer(framebuffer)
						.setViewport(viewport)
						.addBindingSet(bindings)
						.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(m_VertexBuffer).setSlot(0))
						.setIndexBuffer(nvrhi::IndexBufferBinding()
								.setBuffer(m_IndexBuffer)
								.setFormat(
									sizeof(ImDrawIdx) == 2 ? nvrhi::Format::R16_UINT : nvrhi::Format::R32_UINT)));
				commandList->setPushConstants(&projection, sizeof(projection));
				commandList->drawIndexed(nvrhi::DrawArguments()
						.setVertexCount(command.ElemCount)
						.setStartIndexLocation(command.IdxOffset + globalIndexOffset)
						.setStartVertexLocation(command.VtxOffset + globalVertexOffset));
			}
			globalVertexOffset += static_cast<uint32_t>(drawList->VtxBuffer.Size);
			globalIndexOffset += static_cast<uint32_t>(drawList->IdxBuffer.Size);
		}

		// The command list keeps what it used alive until the GPU is done with it
		m_FrameTextures.clear();
	}

	void ImGuiRenderer::UpdateTexture(nvrhi::ICommandList* commandList, ImTextureData& texture)
	{
		if (texture.Status == ImTextureStatus_WantDestroy)
		{
			// NVRHI keeps the texture alive while command lists in flight still use it
			DestroyTexture(texture);
			return;
		}

		if (texture.Status == ImTextureStatus_WantCreate)
		{
			LS_CORE_ASSERT(texture.Format == ImTextureFormat_RGBA32, "The ImGui renderer only creates RGBA textures");
			nvrhi::TextureDesc desc;
			desc.width = static_cast<uint32_t>(texture.Width);
			desc.height = static_cast<uint32_t>(texture.Height);
			desc.format = nvrhi::Format::RGBA8_UNORM;
			desc.debugName = "ImGui texture";
			desc.isShaderResource = true;
			desc.initialState = nvrhi::ResourceStates::ShaderResource;
			desc.keepInitialState = true;
			nvrhi::TextureHandle created = m_Device->createTexture(desc);
			if (!created)
				return;
			texture.SetTexID(ToTextureId(created.Get()));
			texture.BackendUserData = new nvrhi::TextureHandle(std::move(created));
		}

		// The whole texture is uploaded, for updates too: NVRHI writes whole mip levels, and the font atlas, the main
		// texture that changes, is small and changes rarely
		auto* handle = static_cast<nvrhi::TextureHandle*>(texture.BackendUserData);
		commandList->writeTexture(*handle, 0, 0, texture.GetPixels(), static_cast<size_t>(texture.GetPitch()));
		texture.SetStatus(ImTextureStatus_OK);
	}

	void ImGuiRenderer::DestroyTexture(ImTextureData& texture)
	{
		delete static_cast<nvrhi::TextureHandle*>(texture.BackendUserData);
		texture.BackendUserData = nullptr;
		texture.SetTexID(ImTextureID_Invalid);
		texture.SetStatus(ImTextureStatus_Destroyed);
	}

	nvrhi::IBindingSet* ImGuiRenderer::GetBindingSet(nvrhi::ITexture* texture)
	{
		if (texture == nullptr)
			return nullptr;
		if (const auto found = m_BindingSets.find(texture); found != m_BindingSets.end())
			return found->second;
		nvrhi::BindingSetHandle bindings =
			m_Device->createBindingSet(nvrhi::BindingSetDesc()
										   .addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(glm::mat4)))
										   .addItem(nvrhi::BindingSetItem::Texture_SRV(0, texture))
										   .addItem(nvrhi::BindingSetItem::Sampler(0, m_Sampler)),
				m_BindingLayout);
		nvrhi::IBindingSet* raw = bindings;
		m_BindingSets.emplace(texture, std::move(bindings));
		return raw;
	}

	void ImGuiRenderer::EnsureBufferSizes(int vertexCount, int indexCount)
	{
		const auto vertexBytes = static_cast<uint64_t>(vertexCount) * sizeof(ImDrawVert);
		if (!m_VertexBuffer || m_VertexBuffer->getDesc().byteSize < vertexBytes)
		{
			// Room to grow, so the buffer isn't recreated every time the UI gets a little bigger
			m_VertexBuffer = m_Device->createBuffer(nvrhi::BufferDesc()
					.setByteSize(vertexBytes + 5000 * sizeof(ImDrawVert))
					.setIsVertexBuffer(true)
					.setDebugName("ImGui vertices")
					.setInitialState(nvrhi::ResourceStates::VertexBuffer)
					.setKeepInitialState(true));
		}
		const auto indexBytes = static_cast<uint64_t>(indexCount) * sizeof(ImDrawIdx);
		if (!m_IndexBuffer || m_IndexBuffer->getDesc().byteSize < indexBytes)
		{
			m_IndexBuffer = m_Device->createBuffer(nvrhi::BufferDesc()
					.setByteSize(indexBytes + 10000 * sizeof(ImDrawIdx))
					.setIsIndexBuffer(true)
					.setDebugName("ImGui indices")
					.setInitialState(nvrhi::ResourceStates::IndexBuffer)
					.setKeepInitialState(true));
		}
	}

}
