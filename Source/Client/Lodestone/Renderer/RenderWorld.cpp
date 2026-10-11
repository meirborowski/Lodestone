#include "Lodestone/Renderer/RenderWorld.h"

#include "Lodestone/Scene/Components.h"
#include "Lodestone/Scene/Scene.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace Lodestone {

	namespace {

		// Shorter than this, a transformed direction has been squashed to nothing
		constexpr float MinDirectionLength = 1e-6f;
		// Spot cones stop short of a hemisphere, where the cone's falloff would divide by zero
		constexpr float MaxSpotAngle = 89.0f;

		// The entity's forward direction (-Z) in world space, unless the transform squashes it
		std::optional<glm::vec3> GetForward(const glm::mat4& world)
		{
			const glm::vec3 forward = -glm::vec3(world[2]);
			const float length = glm::length(forward);
			if (!(length > MinDirectionLength))
				return std::nullopt;
			return forward / length;
		}

		// A camera's view from its world transform: its position and orientation, ignoring scale
		std::optional<glm::mat4> MakeCameraView(const glm::mat4& world)
		{
			const auto forward = GetForward(world);
			if (!forward)
				return std::nullopt;
			// The up direction, made perpendicular to the forward one
			glm::vec3 up = glm::vec3(world[1]);
			up -= *forward * glm::dot(up, *forward);
			if (!(glm::length(up) > MinDirectionLength))
				return std::nullopt;
			const glm::vec3 position(world[3]);
			return glm::lookAtRH(position, position + *forward, glm::normalize(up));
		}

	}

	glm::vec3 RenderView::GetPosition() const
	{
		// The view matrix is rigid, so its inverse is the transpose of its rotation and the negated translation
		const glm::mat3 rotation(View);
		return -(glm::transpose(rotation) * glm::vec3(View[3]));
	}

	glm::mat4 RenderView::GetProjection(float aspectRatio) const
	{
		// Swapping the near and far planes reverses depth, which spreads the depth buffer's precision evenly over
		// distance
		return glm::perspectiveRH_ZO(FieldOfView, std::max(aspectRatio, 1e-4f), FarPlane, NearPlane);
	}

	RenderWorld ExtractRenderWorld(const Scene& scene, float interpolationFactor)
	{
		const entt::registry& registry = scene.GetRegistry();
		RenderWorld world;
		bool foundEnvironment = false;
		bool foundSettings = false;

		struct Visit
		{
			UUID Entity;
			glm::mat4 ParentWorld;
		};
		std::vector<Visit> stack;
		const std::span<const UUID> roots = scene.GetRootEntities();
		for (auto root = roots.rbegin(); root != roots.rend(); ++root)
			stack.push_back({*root, glm::mat4(1.0f)});

		while (!stack.empty())
		{
			const Visit visit = stack.back();
			stack.pop_back();
			const entt::entity handle = scene.FindHandle(visit.Entity);
			if (handle == entt::null)
				continue;

			TransformComponent local = registry.get<TransformComponent>(handle);
			if (const auto* previous = registry.try_get<PreviousTransformComponent>(handle);
				previous != nullptr && interpolationFactor < 1.0f)
				local = InterpolateTransform(previous->Transform, local, std::max(interpolationFactor, 0.0f));
			const glm::mat4 transform = visit.ParentWorld * local.GetMatrix();

			if (const auto* mesh = registry.try_get<MeshRendererComponent>(handle);
				mesh != nullptr && !mesh->Model.IsNil())
			{
				world.Meshes.push_back({
					.Entity = visit.Entity,
					.Model = mesh->Model,
					.Mesh = mesh->Mesh,
					.World = transform,
					.CastShadows = mesh->CastShadows,
				});
			}
			if (const auto* light = registry.try_get<DirectionalLightComponent>(handle))
			{
				if (const auto direction = GetForward(transform))
				{
					world.DirectionalLights.push_back({
						.Entity = visit.Entity,
						.Direction = *direction,
						.Color = light->Color * light->Intensity,
						.CastShadows = light->CastShadows,
					});
				}
			}
			if (const auto* light = registry.try_get<PointLightComponent>(handle);
				light != nullptr && light->Range > 0.0f)
			{
				world.PunctualLights.push_back({
					.Entity = visit.Entity,
					.Position = glm::vec3(transform[3]),
					.Color = light->Color * light->Intensity,
					.Range = light->Range,
				});
			}
			if (const auto* light = registry.try_get<SpotLightComponent>(handle);
				light != nullptr && light->Range > 0.0f)
			{
				if (const auto direction = GetForward(transform))
				{
					const float outer = std::clamp(light->OuterAngle, 0.01f, MaxSpotAngle);
					const float inner = std::clamp(light->InnerAngle, 0.0f, outer);
					world.PunctualLights.push_back({
						.Entity = visit.Entity,
						.Position = glm::vec3(transform[3]),
						.Color = light->Color * light->Intensity,
						.Range = light->Range,
						.Spot = true,
						.Direction = *direction,
						.CosInnerAngle = std::cos(glm::radians(inner)),
						.CosOuterAngle = std::cos(glm::radians(outer)),
					});
				}
			}
			if (const auto* environment = registry.try_get<EnvironmentComponent>(handle);
				environment != nullptr && !foundEnvironment)
			{
				world.Environment = *environment;
				foundEnvironment = true;
			}
			if (const auto* settings = registry.try_get<RenderSettingsComponent>(handle);
				settings != nullptr && !foundSettings)
			{
				world.Settings = *settings;
				foundSettings = true;
			}
			if (const auto* camera = registry.try_get<CameraComponent>(handle);
				camera != nullptr && camera->Primary && !world.Camera)
			{
				if (const auto view = MakeCameraView(transform))
				{
					const float nearPlane = std::max(camera->NearPlane, 1e-4f);
					world.Camera = RenderView{
						.View = *view,
						.FieldOfView = glm::radians(std::clamp(camera->FieldOfView, 1.0f, 179.0f)),
						.NearPlane = nearPlane,
						.FarPlane = std::max(camera->FarPlane, nearPlane * 1.001f),
					};
				}
			}

			const auto& children = registry.get<HierarchyComponent>(handle).Children;
			for (auto child = children.rbegin(); child != children.rend(); ++child)
				stack.push_back({*child, transform});
		}
		return world;
	}

	glm::mat3 GetNormalMatrix(const glm::mat4& world)
	{
		const glm::vec3 x(world[0]);
		const glm::vec3 y(world[1]);
		const glm::vec3 z(world[2]);
		// The cofactor matrix is the determinant times the inverse transpose; it exists even when the inverse doesn't
		const glm::mat3 cofactor(glm::cross(y, z), glm::cross(z, x), glm::cross(x, y));
		return glm::dot(x, glm::cross(y, z)) < 0.0f ? -cofactor : cofactor;
	}

}
