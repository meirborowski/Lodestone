#pragma once

#include "Lodestone/Asset/Model.h"
#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/BoundingBox.h"
#include "Lodestone/Core/Error.h"
#include "Lodestone/Core/UUID.h"
#include "Lodestone/Renderer/RenderAssets.h"
#include "Lodestone/Renderer/RenderResources.h"
#include "Lodestone/Renderer/RenderWorld.h"
#include "Lodestone/Renderer/SceneRenderTarget.h"
#include "Lodestone/Renderer/ShaderTypes.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <expected>
#include <set>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace Lodestone {

	class GraphicsDevice;

	struct SceneRendererConfig
	{
		// The width and height of each shadow cascade's map, in texels
		uint32_t ShadowMapSize = 2048;
		uint32_t ShadowCascadeCount = 4;
	};

	// Draws scenes: physically based materials lit by directional, point and spot lights and by the environment
	// (image-based lighting), with soft cascaded shadows from a directional light, screen-space ambient occlusion,
	// a sky, and transparent surfaces, in HDR, tonemapped to the displayed image (see docs/Decisions/0018-renderer.md).
	//
	// A frame runs these passes (RenderPass): shadow maps; a depth and normal prepass; ambient occlusion from it; the
	// lit opaque surfaces, over the prepass's depth so each pixel is shaded once; the sky behind them; transparent
	// surfaces, back to front; and tonemapping. Meshes the camera (or a cascade) can't see are culled, and each mesh is
	// lit only by the point and spot lights that reach it.
	//
	// Used from the thread that renders. The scene's depth is reversed: 1 at the near plane, 0 at the far plane
	class SceneRenderer
	{
	private:
		struct Passkey
		{
			explicit Passkey() = default;
		};

	public:
		// More are dropped, farthest from the camera first
		static constexpr size_t MaxPunctualLights = 1024;
		// More reaching one mesh are dropped, farthest from the camera first
		static constexpr size_t MaxLightsPerDraw = 64;

		[[nodiscard]] static std::expected<Scope<SceneRenderer>, Error> Create(
			const GraphicsDevice& device, const SceneRendererConfig& config = {});

		SceneRenderer(Passkey passkey, const GraphicsDevice& device, const SceneRendererConfig& config);
		~SceneRenderer();

		SceneRenderer(const SceneRenderer&) = delete;
		SceneRenderer& operator=(const SceneRenderer&) = delete;
		SceneRenderer(SceneRenderer&&) = delete;
		SceneRenderer& operator=(SceneRenderer&&) = delete;

		[[nodiscard]] std::expected<Scope<SceneRenderTarget>, Error> CreateTarget(
			glm::uvec2 size, std::string_view debugName) const;

		// Records a frame into an open command list: the world seen from the view, into the target. Assets the world
		// uses for the first time are loaded and uploaded first. The target's stats describe the frame afterwards
		void Render(nvrhi::ICommandList* commandList, SceneRenderTarget& target, const RenderWorld& world,
			const RenderView& view);

		// Where models and environments come from, and those on the GPU
		RenderAssets& GetAssets() { return *m_Assets; }
		const RenderResources& GetResources() const { return *m_Resources; }
		const SceneRendererConfig& GetConfig() const { return m_Config; }

	private:
		enum class MeshPass : uint8_t
		{
			Shadow,
			Prepass,
			Forward,
		};

		// A primitive of a mesh, placed in the world, to draw
		struct Draw
		{
			const GpuMesh* Mesh = nullptr;
			const GpuPrimitive* Primitive = nullptr;
			const GpuMaterial* Material = nullptr;
			glm::mat4 World{1.0f};
			BoundingBox Bounds;
			bool Mirrored = false;
			// Along the camera's forward direction, to the bounds' center
			float ViewDepth = 0.0f;
			uint32_t LightOffset = 0;
			uint32_t LightCount = 0;
		};

		[[nodiscard]] std::expected<void, Error> CreatePipelines();
		[[nodiscard]] std::expected<void, Error> CreateShadowMap();
		// The pipeline for a pass and a material's kind, created the first time it's needed; null if it can't be
		nvrhi::IGraphicsPipeline* GetMeshPipeline(MeshPass pass, AlphaMode alpha, bool doubleSided, bool mirrored);
		[[nodiscard]] std::expected<void, Error> UpdateBindings(SceneRenderTarget& target) const;

		void CollectDraws(const RenderWorld& world, const RenderView& view, const glm::mat4& viewProjection,
			nvrhi::ICommandList* commandList, RenderStats& stats);
		void AssignLights(const RenderWorld& world, const glm::vec3& cameraPosition, const glm::mat4& viewProjection);
		void DrawMeshes(nvrhi::ICommandList* commandList, MeshPass pass, const std::vector<Draw>& draws,
			nvrhi::IFramebuffer* framebuffer, std::span<nvrhi::IBindingSet* const> sets,
			const glm::mat4* shadowViewProjection, RenderStats& stats, RenderPass statsPass);
		void DrawFullscreen(nvrhi::ICommandList* commandList, nvrhi::IGraphicsPipeline* pipeline,
			nvrhi::IFramebuffer* framebuffer, std::span<nvrhi::IBindingSet* const> sets, const void* constants,
			size_t constantsSize, RenderStats& stats, RenderPass statsPass);

	private:
		nvrhi::IDevice* m_Device;
		SceneRendererConfig m_Config;
		bool m_TimerQueriesSupported;
		Scope<RenderResources> m_Resources;
		Scope<RenderAssets> m_Assets;

		nvrhi::ShaderHandle m_MeshVertexShader;
		nvrhi::ShaderHandle m_ShadowVertexShader;
		nvrhi::ShaderHandle m_ShadowMaskedPixelShader;
		nvrhi::ShaderHandle m_PrepassPixelShader;
		nvrhi::ShaderHandle m_PrepassMaskedPixelShader;
		nvrhi::ShaderHandle m_ForwardPixelShader;
		nvrhi::ShaderHandle m_ForwardMaskedPixelShader;
		nvrhi::ShaderHandle m_ForwardBlendedPixelShader;
		nvrhi::ShaderHandle m_FullscreenVertexShader;
		nvrhi::ShaderHandle m_OcclusionPixelShader;
		nvrhi::ShaderHandle m_BlurPixelShader;
		nvrhi::ShaderHandle m_SkyPixelShader;
		nvrhi::ShaderHandle m_TonemapPixelShader;
		nvrhi::InputLayoutHandle m_MeshInputLayout;
		nvrhi::InputLayoutHandle m_ShadowInputLayout;

		nvrhi::BindingLayoutHandle m_OcclusionLayout;
		nvrhi::BindingLayoutHandle m_TonemapLayout;
		nvrhi::GraphicsPipelineHandle m_OcclusionPipeline;
		nvrhi::GraphicsPipelineHandle m_BlurPipeline;
		nvrhi::GraphicsPipelineHandle m_SkyPipeline;
		nvrhi::GraphicsPipelineHandle m_TonemapPipeline;
		// By pass, alpha mode, double-sidedness and mirroring
		std::array<nvrhi::GraphicsPipelineHandle, 3 * 3 * 2 * 2> m_MeshPipelines;
		std::array<bool, 3 * 3 * 2 * 2> m_MeshPipelineFailed{};

		nvrhi::TextureHandle m_ShadowMap;
		std::vector<nvrhi::FramebufferHandle> m_ShadowFramebuffers;

		// Rebuilt every frame; kept to reuse their memory
		std::vector<Draw> m_OpaqueDraws;
		std::vector<Draw> m_TransparentDraws;
		std::vector<Draw> m_ShadowCasters;
		std::vector<Draw> m_CascadeDraws;
		std::vector<ShaderTypes::PunctualLight> m_Lights;
		std::vector<uint32_t> m_LightIndices;

		// Problems already reported, so they're logged once rather than every frame
		std::set<std::pair<UUID, uint32_t>> m_ReportedMissingMeshes;
		bool m_ReportedLightLimit = false;
		bool m_ReportedDirectionalLightLimit = false;
	};

}
