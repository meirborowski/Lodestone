#pragma once

#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/Error.h"

#include <imgui.h>
#include <nvrhi/nvrhi.h>

#include <expected>
#include <unordered_map>
#include <vector>

namespace Lodestone {

	// Renders Dear ImGui's draw data through NVRHI. Dear ImGui's own textures, such as its font atlas, are created and
	// updated as it asks (ImGuiBackendFlags_RendererHasTextures); the engine's textures, such as a viewport's render
	// target, are shown with GetTextureId(). Uses the current ImGui context, which must outlive the renderer
	class ImGuiRenderer
	{
	private:
		struct Passkey
		{
			explicit Passkey() = default;
		};

	public:
		// The renderer draws into framebuffers that match framebufferInfo
		[[nodiscard]] static std::expected<Scope<ImGuiRenderer>, Error> Create(
			nvrhi::IDevice* device, const nvrhi::FramebufferInfo& framebufferInfo);

		ImGuiRenderer(Passkey passkey, nvrhi::IDevice* device);
		~ImGuiRenderer();

		ImGuiRenderer(const ImGuiRenderer&) = delete;
		ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;
		ImGuiRenderer(ImGuiRenderer&&) = delete;
		ImGuiRenderer& operator=(ImGuiRenderer&&) = delete;

		// An ID that shows an engine texture with ImGui::Image(). The texture must be sampleable (isShaderResource)
		// and in the ShaderResource state; the renderer keeps it alive until the frame that draws it is rendered
		ImTextureID GetTextureId(nvrhi::ITexture* texture);

		// Records the frame's draw data (ImGui::GetDrawData() after ImGui::Render()) into an open command list
		void Render(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* framebuffer, ImDrawData* drawData);

	private:
		void UpdateTexture(nvrhi::ICommandList* commandList, ImTextureData& texture);
		void DestroyTexture(ImTextureData& texture);
		nvrhi::IBindingSet* GetBindingSet(nvrhi::ITexture* texture);
		void EnsureBufferSizes(int vertexCount, int indexCount);

	private:
		nvrhi::IDevice* m_Device;
		nvrhi::ShaderHandle m_VertexShader;
		nvrhi::ShaderHandle m_PixelShader;
		nvrhi::InputLayoutHandle m_InputLayout;
		nvrhi::BindingLayoutHandle m_BindingLayout;
		nvrhi::SamplerHandle m_Sampler;
		nvrhi::GraphicsPipelineHandle m_Pipeline;
		nvrhi::BufferHandle m_VertexBuffer;
		nvrhi::BufferHandle m_IndexBuffer;
		// Engine textures shown this frame, kept alive until it's rendered
		std::vector<nvrhi::TextureHandle> m_FrameTextures;
		// Binding sets for the textures drawn this frame
		std::unordered_map<nvrhi::ITexture*, nvrhi::BindingSetHandle> m_BindingSets;
	};

}
