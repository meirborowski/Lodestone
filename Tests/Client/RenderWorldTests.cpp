#include "Lodestone/Renderer/RenderWorld.h"

#include "Lodestone/Scene/Components.h"
#include "Lodestone/Scene/Entity.h"
#include "Lodestone/Scene/RenderComponents.h"
#include "Lodestone/Scene/Scene.h"

#include <doctest/doctest.h>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <cmath>

namespace Lodestone {

	namespace {

		const UUID ModelId(0x1111111111114111ull, 0x8111111111111111ull);

		bool Near(const glm::vec3& a, const glm::vec3& b, float tolerance = 1e-5f)
		{
			return glm::length(a - b) <= tolerance;
		}

		Entity CreateMesh(Scene& scene, std::string_view name, const glm::vec3& position, uint32_t mesh = 0)
		{
			Entity entity = scene.CreateEntity(name);
			entity.GetTransform().Position = position;
			entity.Add<MeshRendererComponent>(MeshRendererComponent{.Model = ModelId, .Mesh = mesh});
			return entity;
		}

	}

	TEST_CASE("Extraction collects meshes in hierarchy order, with world transforms")
	{
		Scene scene;
		Entity first = CreateMesh(scene, "First", glm::vec3(1.0f, 0.0f, 0.0f), 0);
		Entity second = CreateMesh(scene, "Second", glm::vec3(-5.0f, 0.0f, 0.0f), 1);
		Entity child = CreateMesh(scene, "Child", glm::vec3(0.0f, 2.0f, 0.0f), 2);
		REQUIRE(scene.SetParent(child, first).has_value());
		// A mesh renderer without a model draws nothing
		scene.CreateEntity("Empty").Add<MeshRendererComponent>();

		const RenderWorld world = ExtractRenderWorld(scene);

		REQUIRE(world.Meshes.size() == 3);
		CHECK(world.Meshes[0].Entity == first.GetId());
		CHECK(world.Meshes[1].Entity == child.GetId());
		CHECK(world.Meshes[2].Entity == second.GetId());
		CHECK(world.Meshes[1].Mesh == 2);
		CHECK(world.Meshes[1].Model == ModelId);
		CHECK(Near(glm::vec3(world.Meshes[1].World[3]), glm::vec3(1.0f, 2.0f, 0.0f)));
		CHECK(world.Meshes[0].CastShadows);
		CHECK_FALSE(world.Camera.has_value());
	}

	TEST_CASE("Extraction interpolates from the previous tick's transform")
	{
		Scene scene;
		Entity parent = scene.CreateEntity("Parent");
		parent.GetTransform().Position = glm::vec3(10.0f, 0.0f, 0.0f);
		parent.Add<PreviousTransformComponent>(PreviousTransformComponent{.Transform = TransformComponent{}});
		Entity mesh = CreateMesh(scene, "Mesh", glm::vec3(0.0f, 1.0f, 0.0f));
		REQUIRE(scene.SetParent(mesh, parent).has_value());

		const RenderWorld quarter = ExtractRenderWorld(scene, 0.25f);
		const RenderWorld current = ExtractRenderWorld(scene);

		REQUIRE(quarter.Meshes.size() == 1);
		CHECK(Near(glm::vec3(quarter.Meshes[0].World[3]), glm::vec3(2.5f, 1.0f, 0.0f)));
		CHECK(Near(glm::vec3(current.Meshes[0].World[3]), glm::vec3(10.0f, 1.0f, 0.0f)));
	}

	TEST_CASE("Lights shine along the entity's -Z, with their color times their intensity")
	{
		Scene scene;
		Entity sun = scene.CreateEntity("Sun");
		sun.GetTransform().Rotation = glm::angleAxis(glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		sun.Add<DirectionalLightComponent>(
			DirectionalLightComponent{.Color = glm::vec3(1.0f, 0.5f, 0.25f), .Intensity = 4.0f, .CastShadows = false});
		Entity bulb = scene.CreateEntity("Bulb");
		bulb.GetTransform().Position = glm::vec3(0.0f, 3.0f, 0.0f);
		bulb.Add<PointLightComponent>(PointLightComponent{.Color = glm::vec3(1.0f), .Intensity = 2.0f, .Range = 5.0f});
		Entity spot = scene.CreateEntity("Spot");
		spot.Add<SpotLightComponent>(SpotLightComponent{.InnerAngle = 20.0f, .OuterAngle = 30.0f});
		// Out of range, or squashed so they have no direction: left out
		scene.CreateEntity("No Range").Add<PointLightComponent>(PointLightComponent{.Range = 0.0f});
		Entity flat = scene.CreateEntity("Flat");
		flat.GetTransform().Scale = glm::vec3(1.0f, 1.0f, 0.0f);
		flat.Add<DirectionalLightComponent>();

		const RenderWorld world = ExtractRenderWorld(scene);

		REQUIRE(world.DirectionalLights.size() == 1);
		CHECK(Near(world.DirectionalLights[0].Direction, glm::vec3(0.0f, -1.0f, 0.0f)));
		CHECK(Near(world.DirectionalLights[0].Color, glm::vec3(4.0f, 2.0f, 1.0f)));
		CHECK_FALSE(world.DirectionalLights[0].CastShadows);

		REQUIRE(world.PunctualLights.size() == 2);
		const RenderPunctualLight& point = world.PunctualLights[0];
		CHECK_FALSE(point.Spot);
		CHECK(Near(point.Position, glm::vec3(0.0f, 3.0f, 0.0f)));
		CHECK(Near(point.Color, glm::vec3(2.0f)));
		CHECK(point.Range == 5.0f);
		const RenderPunctualLight& cone = world.PunctualLights[1];
		CHECK(cone.Spot);
		CHECK(Near(cone.Direction, glm::vec3(0.0f, 0.0f, -1.0f)));
		CHECK(cone.CosInnerAngle == doctest::Approx(std::cos(glm::radians(20.0f))));
		CHECK(cone.CosOuterAngle == doctest::Approx(std::cos(glm::radians(30.0f))));
	}

	TEST_CASE("Spot light angles are kept within a cone narrower than a hemisphere")
	{
		Scene scene;
		scene.CreateEntity("Wide").Add<SpotLightComponent>(
			SpotLightComponent{.InnerAngle = 120.0f, .OuterAngle = 100.0f});

		const RenderWorld world = ExtractRenderWorld(scene);

		REQUIRE(world.PunctualLights.size() == 1);
		CHECK(world.PunctualLights[0].CosOuterAngle > 0.0f);
		CHECK(world.PunctualLights[0].CosInnerAngle >= world.PunctualLights[0].CosOuterAngle);
	}

	TEST_CASE("The first environment, render settings and primary camera win")
	{
		Scene scene;
		scene.CreateEntity("Secondary").Add<CameraComponent>(CameraComponent{.Primary = false});
		Entity camera = scene.CreateEntity("Camera");
		camera.GetTransform().Position = glm::vec3(0.0f, 0.0f, 5.0f);
		camera.Add<CameraComponent>(CameraComponent{.FieldOfView = 90.0f, .NearPlane = 0.5f, .FarPlane = 50.0f});
		scene.CreateEntity("Another Camera").Add<CameraComponent>();
		scene.CreateEntity("Environment").Add<EnvironmentComponent>(EnvironmentComponent{.Intensity = 2.0f});
		scene.CreateEntity("Other Environment").Add<EnvironmentComponent>(EnvironmentComponent{.Intensity = 3.0f});
		scene.CreateEntity("Settings").Add<RenderSettingsComponent>(RenderSettingsComponent{.Exposure = 1.5f});

		const RenderWorld world = ExtractRenderWorld(scene);

		CHECK(world.Environment.Intensity == 2.0f);
		CHECK(world.Settings.Exposure == 1.5f);
		REQUIRE(world.Camera.has_value());
		CHECK(world.Camera->FieldOfView == doctest::Approx(glm::radians(90.0f)));
		CHECK(world.Camera->NearPlane == 0.5f);
		CHECK(world.Camera->FarPlane == 50.0f);
		CHECK(Near(world.Camera->GetPosition(), glm::vec3(0.0f, 0.0f, 5.0f)));
		CHECK(Near(glm::vec3(world.Camera->View * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)), glm::vec3(0.0f, 0.0f, -5.0f)));
	}

	TEST_CASE("A scene without an environment or render settings gets the defaults")
	{
		Scene scene;

		const RenderWorld world = ExtractRenderWorld(scene);

		CHECK(world.Environment.Environment.IsNil());
		CHECK(world.Environment.ShowSky);
		CHECK(world.Settings.AmbientOcclusion);
		CHECK(world.Meshes.empty());
	}

	TEST_CASE("A view's projection reverses depth, and its position is the camera's")
	{
		const RenderView view{
			.View = glm::lookAtRH(glm::vec3(3.0f, 2.0f, 1.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
			.NearPlane = 0.5f,
			.FarPlane = 100.0f,
		};
		const glm::mat4 projection = view.GetProjection(2.0f);
		const auto depthAt = [&](float distance)
		{
			const glm::vec4 clip = projection * glm::vec4(0.0f, 0.0f, -distance, 1.0f);
			return clip.z / clip.w;
		};

		CHECK(depthAt(0.5f) == doctest::Approx(1.0f));
		CHECK(depthAt(100.0f) == doctest::Approx(0.0f).epsilon(1e-5));
		CHECK(depthAt(10.0f) > depthAt(20.0f));
		CHECK(Near(view.GetPosition(), glm::vec3(3.0f, 2.0f, 1.0f), 1e-4f));
	}

	TEST_CASE("Normal matrices keep normals perpendicular to surfaces, even for squashed and mirrored meshes")
	{
		// A plane's tangent stretched by a non-uniform scale: the transformed normal must stay perpendicular to it
		const glm::mat4 stretched = glm::rotate(glm::mat4(1.0f), 0.7f, glm::vec3(0.0f, 0.0f, 1.0f)) *
			glm::scale(glm::mat4(1.0f), glm::vec3(3.0f, 1.0f, 1.0f));
		const glm::vec3 tangent = glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f));
		const glm::vec3 normal = glm::normalize(glm::vec3(1.0f, -1.0f, 0.0f));
		const glm::vec3 worldTangent = glm::vec3(stretched * glm::vec4(tangent, 0.0f));
		const glm::vec3 worldNormal = GetNormalMatrix(stretched) * normal;
		CHECK(std::abs(glm::dot(worldTangent, worldNormal)) < 1e-5f);

		// Mirroring keeps an outward normal outward
		const glm::mat4 mirrored = glm::scale(glm::mat4(1.0f), glm::vec3(-1.0f, 1.0f, 1.0f));
		CHECK(Near(
			glm::normalize(GetNormalMatrix(mirrored) * glm::vec3(1.0f, 0.0f, 0.0f)), glm::vec3(-1.0f, 0.0f, 0.0f)));

		// Squashing flat leaves normals pointing out of the flat side
		const glm::mat4 flat = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 1.0f));
		CHECK(Near(glm::normalize(GetNormalMatrix(flat) * glm::normalize(glm::vec3(0.0f, 1.0f, 1.0f))),
			glm::vec3(0.0f, 1.0f, 0.0f)));
	}

}
