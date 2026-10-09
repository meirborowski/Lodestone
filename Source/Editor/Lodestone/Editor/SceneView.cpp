#include "Lodestone/Editor/SceneView.h"

#include "Lodestone/Graphics/TextureReadback.h"
#include "Lodestone/Scene/Entity.h"

#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace Lodestone {

	namespace {

		// Soft colors that tell neighboring entities apart, picked by UUID so an entity keeps its color
		constexpr std::array<glm::vec4, 8> Palette = {
			glm::vec4(0.55f, 0.70f, 0.90f, 1.0f),
			glm::vec4(0.60f, 0.85f, 0.60f, 1.0f),
			glm::vec4(0.90f, 0.75f, 0.55f, 1.0f),
			glm::vec4(0.80f, 0.60f, 0.85f, 1.0f),
			glm::vec4(0.55f, 0.85f, 0.85f, 1.0f),
			glm::vec4(0.90f, 0.60f, 0.65f, 1.0f),
			glm::vec4(0.85f, 0.85f, 0.55f, 1.0f),
			glm::vec4(0.70f, 0.70f, 0.75f, 1.0f),
		};
		constexpr glm::vec4 SelectionColor{1.0f, 0.55f, 0.15f, 1.0f};
		// Below this, a world matrix squashes its cube flat and can't be inverted
		constexpr float MinDeterminant = 1e-12f;

		// Where a ray enters the unit cube centered on the origin, as a distance along it, if it hits it
		std::optional<float> IntersectUnitCube(const glm::vec3& origin, const glm::vec3& direction)
		{
			float nearest = 0.0f;
			float farthest = std::numeric_limits<float>::max();
			for (int axis = 0; axis < 3; ++axis)
			{
				if (std::abs(direction[axis]) < 1e-12f)
				{
					// Parallel to this pair of faces: it's inside the slab or misses the cube
					if (origin[axis] < -0.5f || origin[axis] > 0.5f)
						return std::nullopt;
					continue;
				}
				float enter = (-0.5f - origin[axis]) / direction[axis];
				float exit = (0.5f - origin[axis]) / direction[axis];
				if (enter > exit)
					std::swap(enter, exit);
				nearest = std::max(nearest, enter);
				farthest = std::min(farthest, exit);
				if (nearest > farthest)
					return std::nullopt;
			}
			return nearest;
		}

	}

	std::expected<Scope<SceneView>, Error> SceneView::Create(GraphicsDevice& device)
	{
		auto view = CreateScope<SceneView>(Passkey(), device);
		auto renderer = DebugSceneRenderer::Create(device.GetNvrhiDevice());
		if (!renderer)
			return std::unexpected(renderer.error());
		view->m_Renderer = std::move(*renderer);
		view->m_CommandList = device.GetNvrhiDevice()->createCommandList();
		if (!view->m_CommandList)
			return std::unexpected(Error(ErrorCode::DeviceError, "Creating the scene view's command list failed"));
		return view;
	}

	SceneView::SceneView(Passkey /*passkey*/, GraphicsDevice& device)
		: m_Device(&device)
	{
	}

	std::expected<nvrhi::ITexture*, Error> SceneView::PrepareViewport(glm::uvec2 size)
	{
		if (auto fitted = Fit(m_Device->GetNvrhiDevice(), m_Viewport, size, "Viewport"); !fitted)
			return std::unexpected(fitted.error());
		return m_Viewport->GetColor();
	}

	void SceneView::RenderViewport(const EditorContext& context)
	{
		if (m_Viewport)
			Render(context, *m_Viewport);
	}

	std::expected<Image, Error> SceneView::Capture(const EditorContext& context, glm::uvec2 size)
	{
		if (auto fitted = Fit(m_Device->GetNvrhiDevice(), m_Capture, size, "Screenshot"); !fitted)
			return std::unexpected(fitted.error());
		Render(context, *m_Capture);
		return ReadTexture(m_Device->GetNvrhiDevice(), m_Capture->GetColor());
	}

	std::vector<DebugCube> SceneView::BuildCubes(const EditorContext& context)
	{
		const Scene& scene = context.GetScene();
		const UUID selection = context.GetSelection();
		std::vector<DebugCube> cubes;
		cubes.reserve(scene.GetEntityCount());
		for (const auto [handle, id] : scene.GetRegistry().view<const IDComponent>().each())
		{
			// GetWorldMatrix() only reads, but takes an entity handle, which needs a mutable scene
			const Entity entity(handle, const_cast<Scene*>(&scene)); // NOLINT(cppcoreguidelines-pro-type-const-cast)
			const glm::vec4 color =
				id.ID == selection ? SelectionColor : Palette[(id.ID.GetLow() >> 8) % Palette.size()];
			cubes.push_back({.World = scene.GetWorldMatrix(entity), .Color = color});
		}
		return cubes;
	}

	glm::mat4 SceneView::GetViewProjection(const EditorContext& context, glm::uvec2 size)
	{
		const EditorCamera& camera = context.GetCamera();
		const float aspectRatio = static_cast<float>(std::max(size.x, 1u)) / static_cast<float>(std::max(size.y, 1u));
		return camera.GetProjection(aspectRatio) * camera.GetView();
	}

	UUID SceneView::PickEntity(const EditorContext& context, glm::uvec2 size, glm::vec2 point)
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
		const glm::vec3 direction = glm::vec3(farPoint) / farPoint.w - origin;

		const Scene& scene = context.GetScene();
		UUID nearest;
		float nearestDistance = std::numeric_limits<float>::max();
		for (const auto [handle, id] : scene.GetRegistry().view<const IDComponent>().each())
		{
			const Entity entity(handle, const_cast<Scene*>(&scene)); // NOLINT(cppcoreguidelines-pro-type-const-cast)
			const glm::mat4 world = scene.GetWorldMatrix(entity);
			if (std::abs(glm::determinant(world)) < MinDeterminant)
				continue;
			// Distances along the ray are the same in the cube's space, since the direction isn't normalized
			const glm::mat4 toLocal = glm::inverse(world);
			const auto distance = IntersectUnitCube(
				glm::vec3(toLocal * glm::vec4(origin, 1.0f)), glm::vec3(toLocal * glm::vec4(direction, 0.0f)));
			if (distance && *distance < nearestDistance)
			{
				nearestDistance = *distance;
				nearest = id.ID;
			}
		}
		return nearest;
	}

	std::expected<void, Error> SceneView::Fit(
		nvrhi::IDevice* device, Scope<RenderTarget>& target, glm::uvec2 size, const char* debugName)
	{
		if (target)
			return target->Resize(size);
		auto created = RenderTarget::Create(device, size, debugName);
		if (!created)
			return std::unexpected(created.error());
		target = std::move(*created);
		return {};
	}

	void SceneView::Render(const EditorContext& context, const RenderTarget& target)
	{
		const DebugView view{.ViewProjection = GetViewProjection(context, target.GetSize())};
		const std::vector<DebugCube> cubes = BuildCubes(context);

		m_CommandList->open();
		m_Renderer->Render(m_CommandList, target.GetFramebuffer(), view, cubes);
		m_CommandList->close();
		m_Device->GetNvrhiDevice()->executeCommandList(m_CommandList);
	}

}
