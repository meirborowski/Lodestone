#include "Lodestone/Editor/SceneView.h"

#include "Lodestone/Graphics/TextureReadback.h"
#include "Lodestone/Renderer/RenderAssets.h"
#include "Lodestone/Scene/Entity.h"
#include "Lodestone/Scene/RenderComponents.h"

#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <system_error>
#include <utility>

namespace Lodestone {

	namespace {

		constexpr uint32_t GridColor = LineVertex::MakeColor(110, 110, 118, 110);
		constexpr uint32_t AxisColorX = LineVertex::MakeColor(200, 70, 70, 220);
		constexpr uint32_t AxisColorZ = LineVertex::MakeColor(70, 110, 210, 220);
		constexpr uint32_t MarkerColor = LineVertex::MakeColor(175, 175, 180);
		constexpr uint32_t LightColor = LineVertex::MakeColor(255, 210, 80);
		constexpr uint32_t CameraColor = LineVertex::MakeColor(110, 180, 255);
		constexpr uint32_t SelectionColor = LineVertex::MakeColor(255, 140, 40);
		// The grid floats a millimeter above the ground, so it isn't lost in a floor at height 0
		constexpr float GridHeight = 0.001f;
		// How long the line showing which way a light or camera faces is, in meters
		constexpr float DirectionLength = 1.0f;
		// Below this, a world matrix squashes its mesh flat and can't be inverted
		constexpr float MinDeterminant = 1e-12f;

		// GetWorldMatrix() only reads, but takes an entity handle, which needs a mutable scene
		Entity MakeEntity(const Scene& scene, entt::entity handle)
		{
			return {handle, const_cast<Scene*>(&scene)}; // NOLINT(cppcoreguidelines-pro-type-const-cast)
		}

		// The mesh an entity draws, if it has one and its model is loaded
		const ModelMesh* FindMesh(const entt::registry& registry, entt::entity handle, const ModelLookup& models)
		{
			const auto* renderer = registry.try_get<MeshRendererComponent>(handle);
			if (renderer == nullptr || renderer->Model.IsNil() || !models)
				return nullptr;
			const Model* model = models(renderer->Model);
			if (model == nullptr || renderer->Mesh >= model->Meshes.size())
				return nullptr;
			const ModelMesh& mesh = model->Meshes[renderer->Mesh];
			return mesh.Bounds.IsEmpty() ? nullptr : &mesh;
		}

		BoundingBox MakeMarkerBox(const glm::vec3& position)
		{
			const glm::vec3 half(SceneView::MarkerSize * 0.5f);
			return {.Min = position - half, .Max = position + half};
		}

		void AddLine(std::vector<LineVertex>& lines, const glm::vec3& from, const glm::vec3& to, uint32_t color)
		{
			lines.push_back({.Position = from, .Color = color});
			lines.push_back({.Position = to, .Color = color});
		}

		void AddBox(std::vector<LineVertex>& lines, const BoundingBox& box, uint32_t color)
		{
			const auto corner = [&box](int index)
			{
				return glm::vec3((index & 1) != 0 ? box.Max.x : box.Min.x, (index & 2) != 0 ? box.Max.y : box.Min.y,
					(index & 4) != 0 ? box.Max.z : box.Min.z);
			};
			// Each edge joins corners that differ in one axis
			for (int index = 0; index < 8; ++index)
			{
				for (const int axis : {1, 2, 4})
				{
					if ((index & axis) == 0)
						AddLine(lines, corner(index), corner(index | axis), color);
				}
			}
		}

		// Where a ray hits a triangle, from either side, as a distance along it (Moller and Trumbore 1997)
		std::optional<float> IntersectTriangle(const glm::vec3& origin, const glm::vec3& direction, const glm::vec3& a,
			const glm::vec3& b, const glm::vec3& c)
		{
			const glm::vec3 edge1 = b - a;
			const glm::vec3 edge2 = c - a;
			const glm::vec3 p = glm::cross(direction, edge2);
			const float determinant = glm::dot(edge1, p);
			if (std::abs(determinant) < std::numeric_limits<float>::min())
				return std::nullopt;
			const float inverse = 1.0f / determinant;
			const glm::vec3 s = origin - a;
			const float u = glm::dot(s, p) * inverse;
			if (u < 0.0f || u > 1.0f)
				return std::nullopt;
			const glm::vec3 q = glm::cross(s, edge1);
			const float v = glm::dot(direction, q) * inverse;
			if (v < 0.0f || u + v > 1.0f)
				return std::nullopt;
			const float distance = glm::dot(edge2, q) * inverse;
			return distance >= 0.0f ? std::optional<float>(distance) : std::nullopt;
		}

		// Where a ray hits a mesh placed by a world matrix, as a distance along the ray
		std::optional<float> IntersectMesh(
			const ModelMesh& mesh, const glm::mat4& world, const glm::vec3& origin, const glm::vec3& direction)
		{
			if (std::abs(glm::determinant(glm::mat3(world))) < MinDeterminant)
				return std::nullopt;
			// In the mesh's space, distances along the ray stay the same, since the direction isn't normalized there
			const glm::mat4 toLocal = glm::inverse(world);
			const glm::vec3 localOrigin(toLocal * glm::vec4(origin, 1.0f));
			const glm::vec3 localDirection(toLocal * glm::vec4(direction, 0.0f));
			if (!mesh.Bounds.Intersect(localOrigin, localDirection))
				return std::nullopt;

			std::optional<float> nearest;
			const size_t vertexCount = mesh.Vertices.size();
			for (size_t index = 0; index + 2 < mesh.Indices.size(); index += 3)
			{
				const uint32_t a = mesh.Indices[index];
				const uint32_t b = mesh.Indices[index + 1];
				const uint32_t c = mesh.Indices[index + 2];
				if (a >= vertexCount || b >= vertexCount || c >= vertexCount)
					continue;
				const auto distance = IntersectTriangle(localOrigin, localDirection, mesh.Vertices[a].Position,
					mesh.Vertices[b].Position, mesh.Vertices[c].Position);
				if (distance && (!nearest || *distance < *nearest))
					nearest = distance;
			}
			return nearest;
		}

	}

	std::expected<Scope<SceneView>, Error> SceneView::Create(GraphicsDevice& device)
	{
		auto view = CreateScope<SceneView>(Passkey(), device);
		auto renderer = SceneRenderer::Create(device);
		if (!renderer)
			return std::unexpected(renderer.error());
		view->m_Renderer = std::move(*renderer);
		auto lines = LineRenderer::Create(device.GetNvrhiDevice());
		if (!lines)
			return std::unexpected(lines.error());
		view->m_Lines = std::move(*lines);
		view->m_CommandList = device.GetNvrhiDevice()->createCommandList();
		if (!view->m_CommandList)
			return std::unexpected(Error(ErrorCode::DeviceError, "Creating the scene view's command list failed"));
		return view;
	}

	SceneView::SceneView(Passkey /*passkey*/, GraphicsDevice& device)
		: m_Device(&device)
	{
	}

	SceneView::~SceneView() = default;

	std::expected<nvrhi::ITexture*, Error> SceneView::PrepareViewport(glm::uvec2 size)
	{
		if (!m_Viewport)
		{
			auto target = m_Renderer->CreateTarget(size, "Viewport");
			if (!target)
				return std::unexpected(target.error());
			m_Viewport = std::move(*target);
		}
		else if (auto resized = m_Viewport->Resize(size); !resized)
			return std::unexpected(resized.error());
		return m_Viewport->GetColor();
	}

	void SceneView::RenderViewport(const EditorContext& context)
	{
		if (!m_Viewport)
			return;
		Render(context, *m_Viewport);
		m_ViewportRendered = true;
	}

	std::expected<Image, Error> SceneView::Capture(const EditorContext& context, glm::uvec2 size)
	{
		if (!m_Capture)
		{
			auto target = m_Renderer->CreateTarget(size, "Screenshot");
			if (!target)
				return std::unexpected(target.error());
			m_Capture = std::move(*target);
		}
		else if (auto resized = m_Capture->Resize(size); !resized)
			return std::unexpected(resized.error());
		Render(context, *m_Capture);
		m_CaptureRendered = true;
		return ReadTexture(m_Device->GetNvrhiDevice(), m_Capture->GetColor());
	}

	std::optional<RenderStats> SceneView::GetStats() const
	{
		if (m_ViewportRendered)
			return m_Viewport->GetStats();
		if (m_CaptureRendered)
			return m_Capture->GetStats();
		return std::nullopt;
	}

	UUID SceneView::PickEntity(const EditorContext& context, glm::uvec2 size, glm::vec2 point) const
	{
		return PickEntity(context, size, point, GetModelLookup());
	}

	RenderView SceneView::GetRenderView(const EditorContext& context)
	{
		const EditorCamera& camera = context.GetCamera();
		return {
			.View = camera.GetView(),
			.FieldOfView = camera.GetFieldOfView(),
			.NearPlane = EditorCamera::NearPlane,
			.FarPlane = EditorCamera::FarPlane,
		};
	}

	glm::mat4 SceneView::GetViewProjection(const EditorContext& context, glm::uvec2 size)
	{
		const EditorCamera& camera = context.GetCamera();
		const float aspectRatio = static_cast<float>(std::max(size.x, 1u)) / static_cast<float>(std::max(size.y, 1u));
		return camera.GetProjection(aspectRatio) * camera.GetView();
	}

	SceneOverlay SceneView::BuildOverlay(const EditorContext& context, const ModelLookup& models)
	{
		SceneOverlay overlay;
		constexpr auto Extent = static_cast<float>(GridHalfSize);
		for (int line = -GridHalfSize; line <= GridHalfSize; ++line)
		{
			const auto offset = static_cast<float>(line);
			AddLine(overlay.DepthTested, glm::vec3(-Extent, GridHeight, offset), glm::vec3(Extent, GridHeight, offset),
				line == 0 ? AxisColorX : GridColor);
			AddLine(overlay.DepthTested, glm::vec3(offset, GridHeight, -Extent), glm::vec3(offset, GridHeight, Extent),
				line == 0 ? AxisColorZ : GridColor);
		}

		const Scene& scene = context.GetScene();
		const entt::registry& registry = scene.GetRegistry();
		const UUID selection = context.GetSelection();
		for (const auto [handle, id] : registry.view<const IDComponent>().each())
		{
			const bool selected = id.ID == selection;
			const glm::mat4 world = scene.GetWorldMatrix(MakeEntity(scene, handle));
			if (const ModelMesh* mesh = FindMesh(registry, handle, models))
			{
				// Meshes show themselves; the selected one is outlined
				if (selected)
					AddBox(overlay.OnTop, mesh->Bounds.Transform(world), SelectionColor);
				continue;
			}

			uint32_t color = MarkerColor;
			bool directed = false;
			if (registry.any_of<DirectionalLightComponent, SpotLightComponent>(handle))
			{
				color = LightColor;
				directed = true;
			}
			else if (registry.all_of<PointLightComponent>(handle))
				color = LightColor;
			else if (registry.all_of<CameraComponent>(handle))
			{
				color = CameraColor;
				directed = true;
			}
			std::vector<LineVertex>& lines = selected ? overlay.OnTop : overlay.DepthTested;
			color = selected ? SelectionColor : color;
			const glm::vec3 position(world[3]);
			AddBox(lines, MakeMarkerBox(position), color);
			const glm::vec3 forward = -glm::vec3(world[2]);
			if (directed && glm::length(forward) > 0.0f)
				AddLine(lines, position, position + glm::normalize(forward) * DirectionLength, color);
		}
		return overlay;
	}

	UUID SceneView::PickEntity(
		const EditorContext& context, glm::uvec2 size, glm::vec2 point, const ModelLookup& models)
	{
		if (size.x == 0 || size.y == 0)
			return {};
		// The point in clip space (+Y up), on the near and far planes
		const glm::vec2 clip(
			2.0f * point.x / static_cast<float>(size.x) - 1.0f, 1.0f - 2.0f * point.y / static_cast<float>(size.y));
		const glm::mat4 inverse = glm::inverse(GetViewProjection(context, size));
		const glm::vec4 nearPoint = inverse * glm::vec4(clip, 0.0f, 1.0f);
		const glm::vec4 farPoint = inverse * glm::vec4(clip, 1.0f, 1.0f);
		const glm::vec3 origin = glm::vec3(nearPoint) / nearPoint.w;
		const glm::vec3 direction = glm::normalize(glm::vec3(farPoint) / farPoint.w - origin);

		const Scene& scene = context.GetScene();
		const entt::registry& registry = scene.GetRegistry();
		UUID nearest;
		float nearestDistance = std::numeric_limits<float>::max();
		for (const auto [handle, id] : registry.view<const IDComponent>().each())
		{
			const glm::mat4 world = scene.GetWorldMatrix(MakeEntity(scene, handle));
			const ModelMesh* mesh = FindMesh(registry, handle, models);
			const auto distance = mesh != nullptr ? IntersectMesh(*mesh, world, origin, direction)
												  : MakeMarkerBox(glm::vec3(world[3])).Intersect(origin, direction);
			if (distance && *distance < nearestDistance)
			{
				nearestDistance = *distance;
				nearest = id.ID;
			}
		}
		return nearest;
	}

	void SceneView::SyncAssets(const EditorContext& context)
	{
		const AssetRegistry* registry = context.GetAssets();
		RenderAssets& assets = m_Renderer->GetAssets();
		if (registry != m_AssetSource)
		{
			m_AssetSource = registry;
			m_AssetGeneration = context.GetAssetGeneration();
			m_AssetWriteTimes.clear();
			if (registry == nullptr)
			{
				assets.SetSource({});
				return;
			}
			RenderAssetSource source = MakeRenderAssetSource(*registry);
			source.LoadModel = [this, registry, load = std::move(source.LoadModel)](UUID id)
			{
				RecordWriteTime(*registry, id);
				return load(id);
			};
			source.LoadEnvironment = [this, registry, load = std::move(source.LoadEnvironment)](UUID id)
			{
				RecordWriteTime(*registry, id);
				return load(id);
			};
			assets.SetSource(std::move(source));
			return;
		}

		// After a scan, assets whose files changed or went away load again
		if (registry == nullptr || context.GetAssetGeneration() == m_AssetGeneration)
			return;
		m_AssetGeneration = context.GetAssetGeneration();
		for (auto asset = m_AssetWriteTimes.begin(); asset != m_AssetWriteTimes.end();)
		{
			const AssetInfo* info = registry->Find(asset->first);
			std::error_code error;
			const auto writeTime = info != nullptr
				? std::filesystem::last_write_time(registry->GetAbsolutePath(*info), error)
				: asset->second;
			if (info == nullptr || error || writeTime != asset->second)
			{
				assets.Invalidate(asset->first);
				asset = m_AssetWriteTimes.erase(asset);
			}
			else
				++asset;
		}
	}

	void SceneView::RecordWriteTime(const AssetRegistry& registry, UUID id)
	{
		const AssetInfo* info = registry.Find(id);
		if (info == nullptr)
			return;
		std::error_code error;
		const auto writeTime = std::filesystem::last_write_time(registry.GetAbsolutePath(*info), error);
		if (!error)
			m_AssetWriteTimes[id] = writeTime;
	}

	ModelLookup SceneView::GetModelLookup() const
	{
		return [this](UUID id) -> const Model*
		{
			const GpuModel* model = m_Renderer->GetAssets().FindModel(id);
			return model != nullptr ? &model->GetModel() : nullptr;
		};
	}

	void SceneView::Render(const EditorContext& context, SceneRenderTarget& target)
	{
		SyncAssets(context);
		const Simulation* simulation = context.GetSimulation();
		const RenderWorld world =
			ExtractRenderWorld(context.GetScene(), simulation != nullptr ? simulation->GetInterpolationFactor() : 1.0f);
		const RenderView view = GetRenderView(context);
		const glm::uvec2 size = target.GetSize();

		nvrhi::IDevice* device = m_Device->GetNvrhiDevice();
		m_CommandList->open();
		m_Renderer->Render(m_CommandList, target, world, view);
		// After the scene, so the overlay can mark the models it just loaded
		const SceneOverlay overlay = BuildOverlay(context, GetModelLookup());
		const glm::mat4 viewProjection =
			view.GetProjection(static_cast<float>(size.x) / static_cast<float>(size.y)) * view.View;
		m_Lines->Render(m_CommandList, target.GetFramebuffer(), viewProjection, overlay.DepthTested, overlay.OnTop);
		m_CommandList->close();
		device->executeCommandList(m_CommandList);
		// Frees what earlier frames no longer use, which the windowed editor's swapchain otherwise does
		device->runGarbageCollection();
	}

}
