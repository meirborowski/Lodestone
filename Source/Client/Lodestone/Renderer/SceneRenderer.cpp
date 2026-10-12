#include "Lodestone/Renderer/SceneRenderer.h"

#include "Lodestone/Core/Log.h"
#include "Lodestone/Graphics/GraphicsDevice.h"
#include "Lodestone/Graphics/ShaderModule.h"
#include "Lodestone/Renderer/Frustum.h"
#include "Lodestone/Renderer/ShadowCascades.h"

#include <glm/common.hpp>
#include <glm/exponential.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>
#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <tuple>

namespace Lodestone {

	namespace {

		// SPIR-V compiled from the shaders in Source/Client/Shaders at build time
#include "Shaders/AmbientOcclusion_PSBlur.spirv.h"
#include "Shaders/AmbientOcclusion_PSOcclusion.spirv.h"
#include "Shaders/Fullscreen_VSMain.spirv.h"
#include "Shaders/Mesh_PSForward.spirv.h"
#include "Shaders/Mesh_PSForwardBlended.spirv.h"
#include "Shaders/Mesh_PSForwardMasked.spirv.h"
#include "Shaders/Mesh_PSPrepass.spirv.h"
#include "Shaders/Mesh_PSPrepassMasked.spirv.h"
#include "Shaders/Mesh_PSShadowMasked.spirv.h"
#include "Shaders/Mesh_VSMain.spirv.h"
#include "Shaders/Mesh_VSShadow.spirv.h"
#include "Shaders/Sky_PSMain.spirv.h"
#include "Shaders/Tonemap_PSMain.spirv.h"

		// The shadowed light's angular radius per unit of RenderSettingsComponent::ShadowSoftness, in degrees
		constexpr float ShadowSoftnessDegrees = 1.5f;
		constexpr float MaxShadowSoftness = 10.0f;
		// Shadows start fading out at this fraction of the shadow distance
		constexpr float ShadowFadeStart = 0.9f;
		// Pushes shadow casters' depth away from the light where they slope toward it, against shadow acne
		constexpr float ShadowSlopeBias = 1.5f;
		constexpr float MinAmbientOcclusionRadius = 0.01f;
		constexpr float MaxExposure = 20.0f;

		size_t GetMeshPipelineIndex(size_t pass, AlphaMode alpha, bool doubleSided, bool mirrored)
		{
			return ((pass * 3 + static_cast<size_t>(alpha)) * 2 + (doubleSided ? 1 : 0)) * 2 + (mirrored ? 1 : 0);
		}

		bool IntersectsSphere(const BoundingBox& box, const glm::vec3& center, float radius)
		{
			const glm::vec3 offset = glm::clamp(center, box.Min, box.Max) - center;
			return glm::dot(offset, offset) <= radius * radius;
		}

		ShaderTypes::DrawConstants MakeDrawConstants(const glm::mat4& world, bool mirrored)
		{
			ShaderTypes::DrawConstants constants;
			constants.World = world;
			const glm::mat3 normal = GetNormalMatrix(world);
			for (glm::length_t row = 0; row < 3; ++row)
				constants.NormalMatrix[row] = glm::vec4(normal[0][row], normal[1][row], normal[2][row], 0.0f);
			constants.NormalMatrix[0].w = mirrored ? -1.0f : 1.0f;
			return constants;
		}

		ShaderTypes::PunctualLight ToShaderLight(const RenderPunctualLight& light)
		{
			// glTF's cone falloff: saturate(cos(angle) * scale + offset), full inside the inner angle and none outside
			// the outer. A point light's covers every direction
			float scale = 0.0f;
			float offset = 1.0f;
			if (light.Spot)
			{
				scale = 1.0f / std::max(light.CosInnerAngle - light.CosOuterAngle, 1e-4f);
				offset = -light.CosOuterAngle * scale;
			}
			return {
				.PositionRange = glm::vec4(light.Position, light.Range),
				.ColorAngleScale = glm::vec4(light.Color, scale),
				.DirectionAngleOffset = glm::vec4(light.Direction, offset),
			};
		}

		nvrhi::ViewportState GetViewport(nvrhi::IFramebuffer* framebuffer)
		{
			return nvrhi::ViewportState().addViewportAndScissorRect(framebuffer->getFramebufferInfo().getViewport());
		}

		nvrhi::VertexAttributeDesc MakeAttribute(const char* name, nvrhi::Format format, size_t offset)
		{
			return nvrhi::VertexAttributeDesc()
				.setName(name)
				.setFormat(format)
				.setOffset(static_cast<uint32_t>(offset))
				.setElementStride(GpuModel::VertexStride);
		}

	}

	std::expected<Scope<SceneRenderer>, Error> SceneRenderer::Create(
		const GraphicsDevice& device, const SceneRendererConfig& config)
	{
		if (config.ShadowMapSize == 0 || config.ShadowCascadeCount == 0 ||
			config.ShadowCascadeCount > ShaderTypes::MaxShadowCascades)
			return std::unexpected(Error(ErrorCode::InvalidArgument,
				fmt::format("The scene renderer needs 1 to {} shadow cascades of at least one texel, not {} of {}",
					ShaderTypes::MaxShadowCascades, config.ShadowCascadeCount, config.ShadowMapSize)));

		auto renderer = CreateScope<SceneRenderer>(Passkey(), device, config);
		auto resources = RenderResources::Create(device);
		if (!resources)
			return std::unexpected(resources.error());
		renderer->m_Resources = std::move(*resources);
		renderer->m_Assets = CreateScope<RenderAssets>(*renderer->m_Resources);
		if (auto created = renderer->CreatePipelines(); !created)
			return std::unexpected(created.error());
		if (auto created = renderer->CreateShadowMap(); !created)
			return std::unexpected(created.error());
		return renderer;
	}

	SceneRenderer::SceneRenderer(Passkey /*passkey*/, const GraphicsDevice& device, const SceneRendererConfig& config)
		: m_Device(device.GetNvrhiDevice()),
		  m_Config(config),
		  m_TimerQueriesSupported(device.GetInfo().SupportsTimerQueries)
	{
	}

	SceneRenderer::~SceneRenderer() = default;

	std::expected<Scope<SceneRenderTarget>, Error> SceneRenderer::CreateTarget(
		glm::uvec2 size, std::string_view debugName) const
	{
		auto target = CreateScope<SceneRenderTarget>(SceneRenderTarget::Passkey(), m_Device, debugName);
		if (auto resized = target->Resize(size); !resized)
			return std::unexpected(resized.error());
		return target;
	}

	std::expected<void, Error> SceneRenderer::CreatePipelines()
	{
		const auto shader =
			[this](nvrhi::ShaderType type, const char* entryPoint, const uint8_t* code, size_t size, const char* name)
		{ return CreateShaderModule(m_Device, type, entryPoint, code, size, name); };
		using Type = nvrhi::ShaderType;
		m_MeshVertexShader =
			shader(Type::Vertex, "VSMain", g_Mesh_VSMain_spirv, sizeof(g_Mesh_VSMain_spirv), "Mesh VS");
		m_ShadowVertexShader =
			shader(Type::Vertex, "VSShadow", g_Mesh_VSShadow_spirv, sizeof(g_Mesh_VSShadow_spirv), "Mesh shadow VS");
		m_ShadowMaskedPixelShader = shader(Type::Pixel, "PSShadowMasked", g_Mesh_PSShadowMasked_spirv,
			sizeof(g_Mesh_PSShadowMasked_spirv), "Mesh masked shadow PS");
		m_PrepassPixelShader =
			shader(Type::Pixel, "PSPrepass", g_Mesh_PSPrepass_spirv, sizeof(g_Mesh_PSPrepass_spirv), "Mesh prepass PS");
		m_PrepassMaskedPixelShader = shader(Type::Pixel, "PSPrepassMasked", g_Mesh_PSPrepassMasked_spirv,
			sizeof(g_Mesh_PSPrepassMasked_spirv), "Mesh masked prepass PS");
		m_ForwardPixelShader =
			shader(Type::Pixel, "PSForward", g_Mesh_PSForward_spirv, sizeof(g_Mesh_PSForward_spirv), "Mesh forward PS");
		m_ForwardMaskedPixelShader = shader(Type::Pixel, "PSForwardMasked", g_Mesh_PSForwardMasked_spirv,
			sizeof(g_Mesh_PSForwardMasked_spirv), "Mesh masked forward PS");
		m_ForwardBlendedPixelShader = shader(Type::Pixel, "PSForwardBlended", g_Mesh_PSForwardBlended_spirv,
			sizeof(g_Mesh_PSForwardBlended_spirv), "Mesh blended forward PS");
		m_FullscreenVertexShader = shader(
			Type::Vertex, "VSMain", g_Fullscreen_VSMain_spirv, sizeof(g_Fullscreen_VSMain_spirv), "Fullscreen VS");
		m_OcclusionPixelShader = shader(Type::Pixel, "PSOcclusion", g_AmbientOcclusion_PSOcclusion_spirv,
			sizeof(g_AmbientOcclusion_PSOcclusion_spirv), "Ambient occlusion PS");
		m_BlurPixelShader = shader(Type::Pixel, "PSBlur", g_AmbientOcclusion_PSBlur_spirv,
			sizeof(g_AmbientOcclusion_PSBlur_spirv), "Ambient occlusion blur PS");
		m_SkyPixelShader = shader(Type::Pixel, "PSMain", g_Sky_PSMain_spirv, sizeof(g_Sky_PSMain_spirv), "Sky PS");
		m_TonemapPixelShader =
			shader(Type::Pixel, "PSMain", g_Tonemap_PSMain_spirv, sizeof(g_Tonemap_PSMain_spirv), "Tonemap PS");
		if (!m_MeshVertexShader || !m_ShadowVertexShader || !m_ShadowMaskedPixelShader || !m_PrepassPixelShader ||
			!m_PrepassMaskedPixelShader || !m_ForwardPixelShader || !m_ForwardMaskedPixelShader ||
			!m_ForwardBlendedPixelShader || !m_FullscreenVertexShader || !m_OcclusionPixelShader ||
			!m_BlurPixelShader || !m_SkyPixelShader || !m_TonemapPixelShader)
			return std::unexpected(Error(ErrorCode::DeviceError, "Creating the scene renderer's shaders failed"));

		const std::array<nvrhi::VertexAttributeDesc, 4> meshAttributes = {
			MakeAttribute("POSITION", nvrhi::Format::RGB32_FLOAT, offsetof(MeshVertex, Position)),
			MakeAttribute("NORMAL", nvrhi::Format::RGB32_FLOAT, offsetof(MeshVertex, Normal)),
			MakeAttribute("TANGENT", nvrhi::Format::RGBA32_FLOAT, offsetof(MeshVertex, Tangent)),
			MakeAttribute("TEXCOORD", nvrhi::Format::RG32_FLOAT, offsetof(MeshVertex, TexCoord)),
		};
		const std::array<nvrhi::VertexAttributeDesc, 2> shadowAttributes = {
			MakeAttribute("POSITION", nvrhi::Format::RGB32_FLOAT, offsetof(MeshVertex, Position)),
			MakeAttribute("TEXCOORD", nvrhi::Format::RG32_FLOAT, offsetof(MeshVertex, TexCoord)),
		};
		m_MeshInputLayout = m_Device->createInputLayout(
			meshAttributes.data(), static_cast<uint32_t>(meshAttributes.size()), m_MeshVertexShader);
		m_ShadowInputLayout = m_Device->createInputLayout(
			shadowAttributes.data(), static_cast<uint32_t>(shadowAttributes.size()), m_ShadowVertexShader);

		m_OcclusionLayout = m_Device->createBindingLayout(nvrhi::BindingLayoutDesc()
				.setVisibility(nvrhi::ShaderType::Pixel)
				.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
				.addItem(nvrhi::BindingLayoutItem::Texture_SRV(1))
				.addItem(nvrhi::BindingLayoutItem::Sampler(0))
				.addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(ShaderTypes::AmbientOcclusionConstants))));
		m_TonemapLayout = m_Device->createBindingLayout(nvrhi::BindingLayoutDesc()
				.setVisibility(nvrhi::ShaderType::Pixel)
				.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
				.addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(ShaderTypes::TonemapConstants))));
		if (!m_MeshInputLayout || !m_ShadowInputLayout || !m_OcclusionLayout || !m_TonemapLayout)
			return std::unexpected(Error(ErrorCode::DeviceError, "Creating the scene renderer's layouts failed"));

		const auto makeFullscreen = [this](nvrhi::IShader* pixelShader,
										std::initializer_list<nvrhi::IBindingLayout*> layouts,
										const nvrhi::FramebufferInfo& framebuffer, bool behindScene)
		{
			nvrhi::GraphicsPipelineDesc desc;
			desc.setVertexShader(m_FullscreenVertexShader).setPixelShader(pixelShader);
			for (nvrhi::IBindingLayout* layout : layouts)
				desc.addBindingLayout(layout);
			desc.renderState.rasterState.setCullNone();
			desc.renderState.depthStencilState.disableDepthWrite();
			if (behindScene)
				// At the far plane, which only pixels nothing was drawn on still have
				desc.renderState.depthStencilState.enableDepthTest().setDepthFunc(
					nvrhi::ComparisonFunc::GreaterOrEqual);
			else
				desc.renderState.depthStencilState.disableDepthTest();
			return m_Device->createGraphicsPipeline(desc, framebuffer);
		};
		const nvrhi::FramebufferInfo occlusion =
			nvrhi::FramebufferInfo().addColorFormat(SceneRenderTarget::OcclusionFormat);
		const nvrhi::FramebufferInfo forward = nvrhi::FramebufferInfo()
												   .addColorFormat(SceneRenderTarget::HdrFormat)
												   .setDepthFormat(RenderTarget::DepthFormat);
		m_OcclusionPipeline = makeFullscreen(m_OcclusionPixelShader, {m_OcclusionLayout}, occlusion, false);
		m_BlurPipeline = makeFullscreen(m_BlurPixelShader, {m_OcclusionLayout}, occlusion, false);
		m_SkyPipeline = makeFullscreen(
			m_SkyPixelShader, {m_Resources->GetViewLayout(), m_Resources->GetEnvironmentLayout()}, forward, true);
		m_TonemapPipeline =
			makeFullscreen(m_TonemapPixelShader, {m_TonemapLayout}, SceneRenderTarget::GetFramebufferInfo(), false);
		if (!m_OcclusionPipeline || !m_BlurPipeline || !m_SkyPipeline || !m_TonemapPipeline)
			return std::unexpected(Error(ErrorCode::DeviceError, "Creating the scene renderer's pipelines failed"));
		return {};
	}

	std::expected<void, Error> SceneRenderer::CreateShadowMap()
	{
		nvrhi::TextureDesc desc;
		desc.width = m_Config.ShadowMapSize;
		desc.height = m_Config.ShadowMapSize;
		desc.arraySize = m_Config.ShadowCascadeCount;
		desc.dimension = nvrhi::TextureDimension::Texture2DArray;
		desc.format = RenderTarget::DepthFormat;
		desc.debugName = "Shadow map";
		desc.isRenderTarget = true;
		desc.initialState = nvrhi::ResourceStates::ShaderResource;
		desc.keepInitialState = true;
		m_ShadowMap = m_Device->createTexture(desc);
		if (!m_ShadowMap)
			return std::unexpected(Error(ErrorCode::DeviceError,
				fmt::format("Creating {} {}x{} shadow maps failed", m_Config.ShadowCascadeCount, m_Config.ShadowMapSize,
					m_Config.ShadowMapSize)));

		for (uint32_t cascade = 0; cascade < m_Config.ShadowCascadeCount; ++cascade)
		{
			nvrhi::FramebufferHandle framebuffer =
				m_Device->createFramebuffer(nvrhi::FramebufferDesc().setDepthAttachment(
					nvrhi::FramebufferAttachment().setTexture(m_ShadowMap).setArraySlice(cascade)));
			if (!framebuffer)
				return std::unexpected(Error(ErrorCode::DeviceError, "Creating the shadow map framebuffers failed"));
			m_ShadowFramebuffers.push_back(std::move(framebuffer));
		}
		return {};
	}

	nvrhi::IGraphicsPipeline* SceneRenderer::GetMeshPipeline(
		MeshPass pass, AlphaMode alpha, bool doubleSided, bool mirrored)
	{
		const size_t index = GetMeshPipelineIndex(static_cast<size_t>(pass), alpha, doubleSided, mirrored);
		if (m_MeshPipelines[index] || m_MeshPipelineFailed[index])
			return m_MeshPipelines[index];

		nvrhi::GraphicsPipelineDesc desc;
		nvrhi::FramebufferInfo framebuffer;
		// glTF's front faces wind counterclockwise as seen with +Y up; a mirroring transform turns them clockwise
		desc.renderState.rasterState
			.setCullMode(doubleSided ? nvrhi::RasterCullMode::None : nvrhi::RasterCullMode::Back)
			.setFrontCounterClockwise(!mirrored);
		nvrhi::DepthStencilState& depth = desc.renderState.depthStencilState;
		depth.enableDepthTest();
		switch (pass)
		{
			case MeshPass::Shadow:
				desc.setVertexShader(m_ShadowVertexShader)
					.setInputLayout(m_ShadowInputLayout)
					.addBindingLayout(m_Resources->GetMaterialLayout());
				if (alpha == AlphaMode::Mask)
					desc.setPixelShader(m_ShadowMaskedPixelShader);
				desc.renderState.rasterState.setSlopeScaleDepthBias(ShadowSlopeBias);
				// Shadow maps aren't reversed: they're orthographic, so their depth is linear anyway
				depth.enableDepthWrite().setDepthFunc(nvrhi::ComparisonFunc::LessOrEqual);
				framebuffer.setDepthFormat(RenderTarget::DepthFormat);
				break;
			case MeshPass::Prepass:
				desc.setVertexShader(m_MeshVertexShader)
					.setPixelShader(alpha == AlphaMode::Mask ? m_PrepassMaskedPixelShader : m_PrepassPixelShader)
					.setInputLayout(m_MeshInputLayout)
					.addBindingLayout(m_Resources->GetViewLayout())
					.addBindingLayout(m_Resources->GetMaterialLayout());
				depth.enableDepthWrite().setDepthFunc(nvrhi::ComparisonFunc::GreaterOrEqual);
				framebuffer.addColorFormat(SceneRenderTarget::NormalFormat).setDepthFormat(RenderTarget::DepthFormat);
				break;
			case MeshPass::Forward:
				desc.setVertexShader(m_MeshVertexShader)
					.setInputLayout(m_MeshInputLayout)
					.addBindingLayout(m_Resources->GetViewLayout())
					.addBindingLayout(m_Resources->GetEnvironmentLayout())
					.addBindingLayout(m_Resources->GetMaterialLayout());
				if (alpha == AlphaMode::Opaque)
				{
					desc.setPixelShader(m_ForwardPixelShader);
				}
				else if (alpha == AlphaMode::Mask)
				{
					desc.setPixelShader(m_ForwardMaskedPixelShader);
				}
				else
				{
					desc.setPixelShader(m_ForwardBlendedPixelShader);
					desc.renderState.blendState.targets[0]
						.setBlendEnable(true)
						.setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
						.setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
						.setSrcBlendAlpha(nvrhi::BlendFactor::One)
						.setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
				}
				// Opaque surfaces match the prepass's depth exactly; transparent ones are tested against it
				depth.disableDepthWrite().setDepthFunc(nvrhi::ComparisonFunc::GreaterOrEqual);
				framebuffer.addColorFormat(SceneRenderTarget::HdrFormat).setDepthFormat(RenderTarget::DepthFormat);
				break;
		}

		m_MeshPipelines[index] = m_Device->createGraphicsPipeline(desc, framebuffer);
		if (!m_MeshPipelines[index])
		{
			m_MeshPipelineFailed[index] = true;
			LS_CORE_ERROR("Creating a scene pipeline failed (pass {}, alpha mode {}, double-sided {}, mirrored {}), so "
						  "meshes that need it aren't drawn",
				static_cast<int>(pass), static_cast<int>(alpha), doubleSided, mirrored);
		}
		return m_MeshPipelines[index];
	}

	std::expected<void, Error> SceneRenderer::UpdateBindings(SceneRenderTarget& target) const
	{
		if (target.m_BindingsValid)
			return {};
		const RenderResources& resources = *m_Resources;
		target.m_ViewBindings = m_Device->createBindingSet(
			nvrhi::BindingSetDesc()
				.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, target.m_FrameConstants))
				.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, target.m_PunctualLights))
				.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(1, target.m_LightIndices))
				.addItem(nvrhi::BindingSetItem::Texture_SRV(2, target.m_Occlusion))
				.addItem(nvrhi::BindingSetItem::Texture_SRV(3, m_ShadowMap, nvrhi::Format::UNKNOWN,
					nvrhi::AllSubresources, nvrhi::TextureDimension::Texture2DArray))
				.addItem(nvrhi::BindingSetItem::Sampler(0, resources.GetPointClampSampler()))
				.addItem(nvrhi::BindingSetItem::Sampler(1, resources.GetShadowSampler())),
			resources.GetViewLayout());
		const auto makeOcclusionBindings = [&](nvrhi::ITexture* input)
		{
			return m_Device->createBindingSet(
				nvrhi::BindingSetDesc()
					.addItem(nvrhi::BindingSetItem::Texture_SRV(0, target.GetDepth()))
					.addItem(nvrhi::BindingSetItem::Texture_SRV(1, input))
					.addItem(nvrhi::BindingSetItem::Sampler(0, resources.GetPointClampSampler()))
					.addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(ShaderTypes::AmbientOcclusionConstants))),
				m_OcclusionLayout);
		};
		target.m_OcclusionBindings = makeOcclusionBindings(target.m_Normals);
		target.m_BlurBindings = makeOcclusionBindings(target.m_RawOcclusion);
		target.m_TonemapBindings = m_Device->createBindingSet(
			nvrhi::BindingSetDesc()
				.addItem(nvrhi::BindingSetItem::Texture_SRV(0, target.m_Hdr))
				.addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(ShaderTypes::TonemapConstants))),
			m_TonemapLayout);
		if (!target.m_ViewBindings || !target.m_OcclusionBindings || !target.m_BlurBindings ||
			!target.m_TonemapBindings)
			return std::unexpected(Error(
				ErrorCode::DeviceError, fmt::format("Creating the binding sets of '{}' failed", target.m_DebugName)));
		target.m_BindingsValid = true;
		return {};
	}

	void SceneRenderer::CollectDraws(const RenderWorld& world, const RenderView& view, const glm::mat4& viewProjection,
		nvrhi::ICommandList* commandList, RenderStats& stats)
	{
		m_OpaqueDraws.clear();
		m_TransparentDraws.clear();
		m_ShadowCasters.clear();
		const Frustum frustum(viewProjection);
		const glm::vec3 cameraPosition = view.GetPosition();
		// The view matrix's third row is the camera's backward direction in world space
		const glm::vec3 forward = -glm::vec3(view.View[0][2], view.View[1][2], view.View[2][2]);

		for (const RenderMesh& instance : world.Meshes)
		{
			const GpuModel* model = m_Assets->GetModel(instance.Model, commandList);
			if (model == nullptr)
				continue;
			const std::span<const GpuMesh> meshes = model->GetMeshes();
			if (instance.Mesh >= meshes.size())
			{
				if (m_ReportedMissingMeshes.emplace(instance.Model, instance.Mesh).second)
					LS_CORE_WARN("Model {} has no mesh {} (it has {}), so entity {} draws nothing", instance.Model,
						instance.Mesh, meshes.size(), instance.Entity);
				continue;
			}
			const GpuMesh& mesh = meshes[instance.Mesh];
			if (!mesh.VertexBuffer)
				continue;

			const bool mirrored = glm::determinant(glm::mat3(instance.World)) < 0.0f;
			bool visible = false;
			for (const GpuPrimitive& primitive : mesh.Primitives)
			{
				const GpuMaterial& material = model->GetMaterials()[primitive.Material];
				Draw draw{
					.Mesh = &mesh,
					.Primitive = &primitive,
					.Material = &material,
					.World = instance.World,
					.Bounds = primitive.Bounds.Transform(instance.World),
					.Mirrored = mirrored,
				};
				// Transparent surfaces let light through, so they cast no shadows
				if (instance.CastShadows && material.Alpha != AlphaMode::Blend)
					m_ShadowCasters.push_back(draw);
				if (!frustum.Intersects(draw.Bounds))
					continue;
				visible = true;
				draw.ViewDepth = glm::dot(draw.Bounds.GetCenter() - cameraPosition, forward);
				(material.Alpha == AlphaMode::Blend ? m_TransparentDraws : m_OpaqueDraws).push_back(draw);
			}
			stats.VisibleMeshes += visible ? 1 : 0;
		}

		// Opaque surfaces grouped by pipeline, then near to far, so the prepass rejects hidden pixels early.
		// Transparent ones far to near, so each blends over what's behind it. Stable, so equal keys keep the world's
		// order and frames are reproducible
		const auto pipelineKey = [](const Draw& draw)
		{ return std::make_tuple(draw.Material->Alpha, draw.Material->DoubleSided, draw.Mirrored); };
		std::ranges::stable_sort(m_OpaqueDraws, [&](const Draw& a, const Draw& b)
			{ return std::make_tuple(pipelineKey(a), a.ViewDepth) < std::make_tuple(pipelineKey(b), b.ViewDepth); });
		std::ranges::stable_sort(
			m_TransparentDraws, [](const Draw& a, const Draw& b) { return a.ViewDepth > b.ViewDepth; });
	}

	void SceneRenderer::AssignLights(
		const RenderWorld& world, const glm::vec3& cameraPosition, const glm::mat4& viewProjection)
	{
		m_Lights.clear();
		m_LightIndices.clear();

		// The lights that reach anything the camera sees, nearest first
		const Frustum frustum(viewProjection);
		std::vector<std::pair<float, const RenderPunctualLight*>> candidates;
		for (const RenderPunctualLight& light : world.PunctualLights)
		{
			const BoundingBox reach{.Min = light.Position - light.Range, .Max = light.Position + light.Range};
			if (frustum.Intersects(reach))
				candidates.emplace_back(glm::distance(light.Position, cameraPosition), &light);
		}
		std::ranges::stable_sort(candidates, [](const auto& a, const auto& b) { return a.first < b.first; });
		if (candidates.size() > MaxPunctualLights)
		{
			if (!m_ReportedLightLimit)
				LS_CORE_WARN("{} point and spot lights are in view, but only the nearest {} are drawn",
					candidates.size(), MaxPunctualLights);
			m_ReportedLightLimit = true;
			candidates.resize(MaxPunctualLights);
		}
		for (const auto& candidate : candidates)
			m_Lights.push_back(ToShaderLight(*candidate.second));

		// Each draw's lights: those whose range reaches its bounds
		for (std::vector<Draw>* draws : {&m_OpaqueDraws, &m_TransparentDraws})
		{
			for (Draw& draw : *draws)
			{
				draw.LightOffset = static_cast<uint32_t>(m_LightIndices.size());
				for (size_t light = 0;
					light < candidates.size() && m_LightIndices.size() - draw.LightOffset < MaxLightsPerDraw; ++light)
				{
					const RenderPunctualLight& candidate = *candidates[light].second;
					if (IntersectsSphere(draw.Bounds, candidate.Position, candidate.Range))
						m_LightIndices.push_back(static_cast<uint32_t>(light));
				}
				draw.LightCount = static_cast<uint32_t>(m_LightIndices.size() - draw.LightOffset);
			}
		}
	}

	void SceneRenderer::Render(
		nvrhi::ICommandList* commandList, SceneRenderTarget& target, const RenderWorld& world, const RenderView& view)
	{
		const auto start = std::chrono::steady_clock::now();
		RenderStats stats;
		stats.GpuPassTimes = target.m_Stats.GpuPassTimes;
		stats.Meshes = static_cast<uint32_t>(world.Meshes.size());

		// The timings of frames the GPU has finished, oldest first so the latest is kept
		using TimerFrame = SceneRenderTarget::TimerFrame;
		for (size_t i = 0; i < SceneRenderTarget::TimerFrameCount; ++i)
		{
			TimerFrame& frame =
				target.m_TimerFrames[(target.m_NextTimerFrame + i) % SceneRenderTarget::TimerFrameCount];
			if (!frame.InFlight ||
				!std::ranges::all_of(
					frame.Queries, [this](const auto& query) { return m_Device->pollTimerQuery(query); }))
				continue;
			std::array<double, RenderPassCount> times{};
			for (size_t pass = 0; pass < RenderPassCount; ++pass)
				times[pass] = static_cast<double>(m_Device->getTimerQueryTime(frame.Queries[pass])) * 1000.0;
			frame.InFlight = false;
			// Every frame does work (tonemapping covers the image), so a device that measures none has timestamps that
			// don't advance within a command list - MoltenVK's without GPU counters. Its timings mean nothing, so the
			// renderer stops timing, and reports none
			if (std::ranges::all_of(times, [](double time) { return time <= 0.0; }))
			{
				if (m_TimerQueriesSupported)
					LS_CORE_INFO("The GPU measures no time for a frame, so its timestamps can't time work, and GPU "
								 "timings aren't available");
				m_TimerQueriesSupported = false;
				continue;
			}
			stats.GpuPassTimes = times;
		}
		// This frame's queries, unless the next ones are all still in flight
		TimerFrame& timers = target.m_TimerFrames[target.m_NextTimerFrame];
		bool timing = m_TimerQueriesSupported && !timers.InFlight;
		if (timing && !timers.Queries[0])
		{
			for (nvrhi::TimerQueryHandle& query : timers.Queries)
				query = m_Device->createTimerQuery();
			timing = std::ranges::all_of(timers.Queries, [](const auto& query) { return query != nullptr; });
		}
		if (timing)
			target.m_NextTimerFrame = (target.m_NextTimerFrame + 1) % SceneRenderTarget::TimerFrameCount;
		const auto beginPass = [&](RenderPass pass)
		{
			const std::string name(ToString(pass));
			commandList->beginMarker(name.c_str());
			if (timing)
				commandList->beginTimerQuery(timers.Queries[static_cast<size_t>(pass)]);
		};
		const auto endPass = [&](RenderPass pass)
		{
			if (timing)
				commandList->endTimerQuery(timers.Queries[static_cast<size_t>(pass)]);
			commandList->endMarker();
		};

		const glm::uvec2 size = target.GetSize();
		const float aspectRatio = static_cast<float>(size.x) / static_cast<float>(size.y);
		const glm::mat4 projection = view.GetProjection(aspectRatio);
		const glm::mat4 viewProjection = projection * view.View;
		const glm::vec3 cameraPosition = view.GetPosition();

		const EnvironmentMap* environment = m_Assets->GetEnvironment(world.Environment.Environment, commandList);
		CollectDraws(world, view, viewProjection, commandList, stats);
		AssignLights(world, cameraPosition, viewProjection);
		stats.PunctualLights = static_cast<uint32_t>(m_Lights.size());

		ShaderTypes::FrameConstants frame;
		frame.View = view.View;
		frame.ViewProjection = viewProjection;
		frame.InverseViewProjection = glm::inverse(viewProjection);
		frame.CameraPosition = glm::vec4(cameraPosition, 1.0f);
		frame.ViewportSize = glm::vec4(glm::vec2(size), 1.0f / glm::vec2(size));

		// Directional lights, the first with shadows casting them
		const size_t directionalCount =
			std::min<size_t>(world.DirectionalLights.size(), ShaderTypes::MaxDirectionalLights);
		if (world.DirectionalLights.size() > directionalCount && !m_ReportedDirectionalLightLimit)
		{
			LS_CORE_WARN("The scene has {} directional lights, but only the first {} are drawn",
				world.DirectionalLights.size(), directionalCount);
			m_ReportedDirectionalLightLimit = true;
		}
		uint32_t shadowedLight = ShaderTypes::NoShadowedLight;
		for (size_t i = 0; i < directionalCount; ++i)
		{
			const RenderDirectionalLight& light = world.DirectionalLights[i];
			frame.DirectionalLights[i] = {
				.Direction = glm::vec4(light.Direction, 0.0f), .Color = glm::vec4(light.Color, 0.0f)};
			if (shadowedLight == ShaderTypes::NoShadowedLight && light.CastShadows)
				shadowedLight = static_cast<uint32_t>(i);
		}

		// Shadow cascades, reaching back to every caster
		const RenderSettingsComponent& settings = world.Settings;
		std::vector<ShadowCascade> cascades;
		if (shadowedLight != ShaderTypes::NoShadowedLight && settings.ShadowDistance > 0.0f)
		{
			BoundingBox casterBounds;
			for (const Draw& caster : m_ShadowCasters)
				casterBounds.Add(caster.Bounds);
			cascades = ComputeShadowCascades(world.DirectionalLights[shadowedLight].Direction, view, aspectRatio,
				{.CascadeCount = m_Config.ShadowCascadeCount,
					.MapSize = m_Config.ShadowMapSize,
					.ShadowDistance = settings.ShadowDistance},
				casterBounds);
		}
		frame.Counts = glm::uvec4(directionalCount, cascades.size(), shadowedLight, 0);
		frame.CascadeSplits = glm::vec4(std::numeric_limits<float>::max());
		for (size_t i = 0; i < cascades.size(); ++i)
		{
			const auto component = static_cast<glm::length_t>(i);
			frame.CascadeViewProjections[i] = cascades[i].ViewProjection;
			frame.CascadeSplits[component] = cascades[i].SplitDepth;
			frame.CascadeTexelSizes[component] = cascades[i].TexelSize;
			frame.CascadeDepthRanges[component] = cascades[i].DepthRange;
		}
		const float softness = std::clamp(settings.ShadowSoftness, 0.0f, MaxShadowSoftness);
		const float shadowEnd = std::min(settings.ShadowDistance, view.FarPlane);
		frame.Shadow = glm::vec4(std::tan(glm::radians(softness * ShadowSoftnessDegrees)),
			1.0f / static_cast<float>(m_Config.ShadowMapSize), shadowEnd * ShadowFadeStart, shadowEnd);
		stats.DirectionalLights = static_cast<uint32_t>(directionalCount);
		stats.ShadowCascades = static_cast<uint32_t>(cascades.size());

		// The environment, and the background where there's no sky: its average color
		const float intensity = std::max(world.Environment.Intensity, 0.0f);
		glm::vec3 background(0.0f);
		if (environment != nullptr)
		{
			const SphericalHarmonics& irradiance = environment->GetIrradiance();
			for (size_t i = 0; i < irradiance.size(); ++i)
				frame.IrradianceSh[i] = glm::vec4(irradiance[i] * intensity, 0.0f);
			const float rotation = glm::radians(world.Environment.Rotation);
			frame.Environment = glm::vec4(intensity, std::sin(rotation), std::cos(rotation),
				static_cast<float>(environment->GetPrefilteredMaxLevel()));
			background = glm::max(irradiance[0] * 0.282095f * intensity, glm::vec3(0.0f));
		}
		const bool ambientOcclusion = settings.AmbientOcclusion && settings.AmbientOcclusionIntensity > 0.0f;
		const float occlusionRadius = std::max(settings.AmbientOcclusionRadius, MinAmbientOcclusionRadius);
		frame.AmbientOcclusion = glm::vec4(
			ambientOcclusion ? 1.0f : 0.0f, occlusionRadius, std::max(settings.AmbientOcclusionIntensity, 0.0f), 0.0f);

		if (auto reserved = target.ReserveLights(m_Lights.size(), m_LightIndices.size()); !reserved)
		{
			LS_CORE_ERROR("Can't render '{}': {}", target.m_DebugName, reserved.error());
			return;
		}
		if (auto updated = UpdateBindings(target); !updated)
		{
			LS_CORE_ERROR("Can't render '{}': {}", target.m_DebugName, updated.error());
			return;
		}
		if (environment == nullptr)
		{
			// Only when the GPU can't create even the default environment, which was reported
			commandList->clearTextureFloat(target.GetColor(), nvrhi::AllSubresources, nvrhi::Color(0.0f));
			return;
		}
		commandList->writeBuffer(target.m_FrameConstants, &frame, sizeof(frame));
		if (!m_Lights.empty())
			commandList->writeBuffer(
				target.m_PunctualLights, m_Lights.data(), m_Lights.size() * sizeof(ShaderTypes::PunctualLight));
		if (!m_LightIndices.empty())
			commandList->writeBuffer(
				target.m_LightIndices, m_LightIndices.data(), m_LightIndices.size() * sizeof(uint32_t));

		nvrhi::IBindingSet* viewBindings = target.m_ViewBindings;
		const std::array<nvrhi::IBindingSet*, 1> prepassSets = {viewBindings};
		const std::array<nvrhi::IBindingSet*, 2> forwardSets = {viewBindings, environment->GetBindings()};

		beginPass(RenderPass::Shadows);
		for (size_t i = 0; i < cascades.size(); ++i)
		{
			commandList->clearDepthStencilTexture(
				m_ShadowMap, nvrhi::TextureSubresourceSet(0, 1, static_cast<uint32_t>(i), 1), true, 1.0f, false, 0);
			const Frustum cascadeFrustum(cascades[i].ViewProjection);
			m_CascadeDraws.clear();
			for (const Draw& caster : m_ShadowCasters)
			{
				if (cascadeFrustum.Intersects(caster.Bounds))
					m_CascadeDraws.push_back(caster);
			}
			DrawMeshes(commandList, MeshPass::Shadow, m_CascadeDraws, m_ShadowFramebuffers[i], {},
				&cascades[i].ViewProjection, stats, RenderPass::Shadows);
		}
		endPass(RenderPass::Shadows);

		beginPass(RenderPass::Prepass);
		commandList->clearTextureFloat(target.m_Normals, nvrhi::AllSubresources, nvrhi::Color(0.0f));
		commandList->clearDepthStencilTexture(target.GetDepth(), nvrhi::AllSubresources, true, 0.0f, false, 0);
		DrawMeshes(commandList, MeshPass::Prepass, m_OpaqueDraws, target.m_PrepassFramebuffer, prepassSets, nullptr,
			stats, RenderPass::Prepass);
		endPass(RenderPass::Prepass);

		beginPass(RenderPass::AmbientOcclusion);
		if (ambientOcclusion)
		{
			// Reversed depth d is 1 at the near plane n and 0 at the far plane f; the distance is n f / (d (f - n) + n)
			const ShaderTypes::AmbientOcclusionConstants constants{
				.ProjectionScale = glm::vec4(projection[0][0], projection[1][1], 0.0f, 0.0f),
				.DepthToDistance =
					glm::vec4(view.NearPlane * view.FarPlane, view.FarPlane - view.NearPlane, view.NearPlane, 0.0f),
				.ViewportSize = frame.ViewportSize,
				.Radius = occlusionRadius,
				.Intensity = frame.AmbientOcclusion.z,
			};
			const std::array<nvrhi::IBindingSet*, 1> occlusionSets = {target.m_OcclusionBindings};
			const std::array<nvrhi::IBindingSet*, 1> blurSets = {target.m_BlurBindings};
			DrawFullscreen(commandList, m_OcclusionPipeline, target.m_OcclusionFramebuffer, occlusionSets, &constants,
				sizeof(constants), stats, RenderPass::AmbientOcclusion);
			DrawFullscreen(commandList, m_BlurPipeline, target.m_BlurFramebuffer, blurSets, &constants,
				sizeof(constants), stats, RenderPass::AmbientOcclusion);
		}
		endPass(RenderPass::AmbientOcclusion);

		beginPass(RenderPass::Opaque);
		commandList->clearTextureFloat(
			target.m_Hdr, nvrhi::AllSubresources, nvrhi::Color(background.r, background.g, background.b, 1.0f));
		DrawMeshes(commandList, MeshPass::Forward, m_OpaqueDraws, target.m_ForwardFramebuffer, forwardSets, nullptr,
			stats, RenderPass::Opaque);
		endPass(RenderPass::Opaque);

		beginPass(RenderPass::Sky);
		if (world.Environment.ShowSky)
			DrawFullscreen(commandList, m_SkyPipeline, target.m_ForwardFramebuffer, forwardSets, nullptr, 0, stats,
				RenderPass::Sky);
		endPass(RenderPass::Sky);

		beginPass(RenderPass::Transparent);
		DrawMeshes(commandList, MeshPass::Forward, m_TransparentDraws, target.m_ForwardFramebuffer, forwardSets,
			nullptr, stats, RenderPass::Transparent);
		endPass(RenderPass::Transparent);

		beginPass(RenderPass::Tonemap);
		const ShaderTypes::TonemapConstants tonemap{
			.Exposure = std::exp2(std::clamp(settings.Exposure, -MaxExposure, MaxExposure)),
			.Tonemapper = std::min(settings.Tonemapper, static_cast<uint32_t>(Tonemapper::None)),
		};
		const std::array<nvrhi::IBindingSet*, 1> tonemapSets = {target.m_TonemapBindings};
		DrawFullscreen(commandList, m_TonemapPipeline, target.GetFramebuffer(), tonemapSets, &tonemap, sizeof(tonemap),
			stats, RenderPass::Tonemap);
		endPass(RenderPass::Tonemap);

		if (timing)
			timers.InFlight = true;
		stats.CpuTime = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		target.m_Stats = stats;
	}

	void SceneRenderer::DrawMeshes(nvrhi::ICommandList* commandList, MeshPass pass, const std::vector<Draw>& draws,
		nvrhi::IFramebuffer* framebuffer, std::span<nvrhi::IBindingSet* const> sets,
		const glm::mat4* shadowViewProjection, RenderStats& stats, RenderPass statsPass)
	{
		if (draws.empty())
			return;
		const nvrhi::ViewportState viewport = GetViewport(framebuffer);
		for (const Draw& draw : draws)
		{
			nvrhi::IGraphicsPipeline* pipeline =
				GetMeshPipeline(pass, draw.Material->Alpha, draw.Material->DoubleSided, draw.Mirrored);
			if (pipeline == nullptr)
				continue;
			nvrhi::GraphicsState state;
			state.setPipeline(pipeline)
				.setFramebuffer(framebuffer)
				.setViewport(viewport)
				.setIndexBuffer(
					nvrhi::IndexBufferBinding().setBuffer(draw.Mesh->IndexBuffer).setFormat(nvrhi::Format::R32_UINT))
				.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(draw.Mesh->VertexBuffer).setSlot(0));
			for (nvrhi::IBindingSet* set : sets)
				state.addBindingSet(set);
			state.addBindingSet(draw.Material->Bindings);
			commandList->setGraphicsState(state);

			ShaderTypes::DrawConstants constants;
			if (shadowViewProjection != nullptr)
			{
				// The shadow pass takes the whole transform to the shadow map, and needs nothing else
				constants.World = *shadowViewProjection * draw.World;
			}
			else
			{
				constants = MakeDrawConstants(draw.World, draw.Mirrored);
				constants.LightOffset = draw.LightOffset;
				constants.LightCount = draw.LightCount;
			}
			commandList->setPushConstants(&constants, sizeof(constants));
			commandList->drawIndexed(nvrhi::DrawArguments()
					.setVertexCount(draw.Primitive->IndexCount)
					.setStartIndexLocation(draw.Primitive->FirstIndex));

			++stats.DrawCalls;
			++stats.PassDrawCalls[static_cast<size_t>(statsPass)];
			stats.Triangles += draw.Primitive->IndexCount / 3;
		}
	}

	void SceneRenderer::DrawFullscreen(nvrhi::ICommandList* commandList, nvrhi::IGraphicsPipeline* pipeline,
		nvrhi::IFramebuffer* framebuffer, std::span<nvrhi::IBindingSet* const> sets, const void* constants,
		size_t constantsSize, RenderStats& stats, RenderPass statsPass)
	{
		nvrhi::GraphicsState state;
		state.setPipeline(pipeline).setFramebuffer(framebuffer).setViewport(GetViewport(framebuffer));
		for (nvrhi::IBindingSet* set : sets)
			state.addBindingSet(set);
		commandList->setGraphicsState(state);
		if (constants != nullptr)
			commandList->setPushConstants(constants, constantsSize);
		commandList->draw(nvrhi::DrawArguments().setVertexCount(3));
		++stats.DrawCalls;
		++stats.PassDrawCalls[static_cast<size_t>(statsPass)];
		++stats.Triangles;
	}

}
