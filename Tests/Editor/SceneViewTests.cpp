#include "Lodestone/Editor/SceneView.h"

#include "Common/DescribeError.h"
#include "Lodestone/Asset/ModelImporter.h"
#include "Lodestone/Scene/Entity.h"
#include "Lodestone/Scene/RenderComponents.h"

#include <doctest/doctest.h>
#include <glm/ext/scalar_constants.hpp>

#include <algorithm>
#include <filesystem>

namespace Lodestone {

	namespace {

		constexpr glm::uvec2 ImageSize{800, 600};
		constexpr glm::vec2 Center(400.0f, 300.0f);
		constexpr UUID PrimitivesId(0x2000000000004000ull, 0x8000000000000001ull);
		// Meshes of Primitives.glb
		constexpr uint32_t Cube = 1;
		constexpr uint32_t Sphere = 2;
		// The overlay's grid: a line each way for every meter across it
		constexpr auto GridVertexCount = static_cast<size_t>(SceneView::GridHalfSize * 2 + 1) * 2 * 2;
		// A marker is a box: 12 lines
		constexpr size_t MarkerVertexCount = 24;

		const Model& GetPrimitives()
		{
			static const Model Primitives = []
			{
				auto imported =
					ModelImporter::Import(std::filesystem::path(LS_TEST_FIXTURE_DIR) / "Models" / "Primitives.glb");
				REQUIRE_MESSAGE(imported.has_value(), Testing::DescribeError(imported));
				return std::move(*imported);
			}();
			return Primitives;
		}

		// The primitives model, as if the renderer had loaded it
		const Model* Loaded(UUID id)
		{
			return id == PrimitivesId ? &GetPrimitives() : nullptr;
		}

		// No models loaded
		const Model* Unloaded(UUID /*id*/)
		{
			return nullptr;
		}

		// An editor looking down -Z at the origin from 10 m away
		struct Fixture
		{
			Fixture() { Context.GetCamera().LookAt(glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f)); }

			Entity CreateEntity(const char* name, glm::vec3 position, glm::vec3 scale = glm::vec3(1.0f))
			{
				Entity entity = Context.GetScene().CreateEntity(name);
				entity.GetTransform().Position = position;
				entity.GetTransform().Scale = scale;
				return entity;
			}

			Entity CreateMesh(const char* name, uint32_t mesh, glm::vec3 position, glm::vec3 scale = glm::vec3(1.0f))
			{
				Entity entity = CreateEntity(name, position, scale);
				entity.Add<MeshRendererComponent>(MeshRendererComponent{.Model = PrimitivesId, .Mesh = mesh});
				return entity;
			}

			// Where a world position shows in the image, in pixels from its top left corner
			glm::vec2 Project(glm::vec3 position) const
			{
				const glm::vec4 clip = SceneView::GetViewProjection(Context, ImageSize) * glm::vec4(position, 1.0f);
				const glm::vec2 ndc = glm::vec2(clip) / clip.w;
				return {(ndc.x + 1.0f) * 0.5f * static_cast<float>(ImageSize.x),
					(1.0f - ndc.y) * 0.5f * static_cast<float>(ImageSize.y)};
			}

			UUID Pick(glm::vec2 point, const ModelLookup& models = Loaded) const
			{
				return SceneView::PickEntity(Context, ImageSize, point, models);
			}

			EditorContext Context;
		};

		bool HasVertexAt(const std::vector<LineVertex>& lines, const glm::vec3& position)
		{
			return std::ranges::any_of(lines, [&position](const LineVertex& vertex)
				{ return glm::all(glm::lessThan(glm::abs(vertex.Position - position), glm::vec3(1e-4f))); });
		}

	}

	TEST_CASE("The overlay draws the grid and a marker for each entity that draws nothing itself")
	{
		Fixture fixture;
		fixture.CreateEntity("Empty", glm::vec3(1.0f, 2.0f, 3.0f));
		fixture.CreateEntity("Sun", glm::vec3(0.0f, 5.0f, 0.0f)).Add<DirectionalLightComponent>();
		fixture.CreateEntity("Bulb", glm::vec3(-2.0f, 1.0f, 0.0f)).Add<PointLightComponent>();
		fixture.CreateEntity("Camera", glm::vec3(0.0f, 1.0f, 8.0f)).Add<CameraComponent>();

		const SceneOverlay overlay = SceneView::BuildOverlay(fixture.Context, Loaded);

		// Markers, plus a line showing which way the directional light and the camera face
		CHECK(overlay.DepthTested.size() == GridVertexCount + 4 * MarkerVertexCount + 4);
		CHECK(overlay.OnTop.empty());
		const float half = SceneView::MarkerSize * 0.5f;
		CHECK(HasVertexAt(overlay.DepthTested, glm::vec3(1.0f - half, 2.0f - half, 3.0f - half)));
		// The sun faces -Z, as lights do
		CHECK(HasVertexAt(overlay.DepthTested, glm::vec3(0.0f, 5.0f, -1.0f)));
		// The grid's corners
		CHECK(HasVertexAt(overlay.DepthTested, glm::vec3(-20.0f, 0.001f, -20.0f)));
	}

	TEST_CASE("Meshes get markers only until their models load")
	{
		Fixture fixture;
		fixture.CreateMesh("Cube", Cube, glm::vec3(0.0f));
		// A mesh the model doesn't have, and a mesh renderer without a model
		fixture.CreateMesh("Missing", 9, glm::vec3(2.0f, 0.0f, 0.0f));
		fixture.CreateEntity("Unassigned", glm::vec3(-2.0f, 0.0f, 0.0f)).Add<MeshRendererComponent>();

		CHECK(SceneView::BuildOverlay(fixture.Context, Loaded).DepthTested.size() ==
			GridVertexCount + 2 * MarkerVertexCount);
		CHECK(SceneView::BuildOverlay(fixture.Context, Unloaded).DepthTested.size() ==
			GridVertexCount + 3 * MarkerVertexCount);
		CHECK(
			SceneView::BuildOverlay(fixture.Context, {}).DepthTested.size() == GridVertexCount + 3 * MarkerVertexCount);
	}

	TEST_CASE("The selection is outlined over everything: a marker, or a mesh's bounds")
	{
		Fixture fixture;
		const Entity empty = fixture.CreateEntity("Empty", glm::vec3(0.0f));
		const Entity cube = fixture.CreateMesh("Cube", Cube, glm::vec3(3.0f, 0.0f, 0.0f), glm::vec3(2.0f));

		fixture.Context.Select(empty.GetId());
		const SceneOverlay markerSelected = SceneView::BuildOverlay(fixture.Context, Loaded);
		CHECK(markerSelected.OnTop.size() == MarkerVertexCount);
		CHECK(markerSelected.DepthTested.size() == GridVertexCount);

		fixture.Context.Select(cube.GetId());
		const SceneOverlay meshSelected = SceneView::BuildOverlay(fixture.Context, Loaded);
		REQUIRE(meshSelected.OnTop.size() == MarkerVertexCount);
		// The unit cube scaled by 2 around (3, 0, 0)
		CHECK(HasVertexAt(meshSelected.OnTop, glm::vec3(2.0f, -1.0f, -1.0f)));
		CHECK(HasVertexAt(meshSelected.OnTop, glm::vec3(4.0f, 1.0f, 1.0f)));
		CHECK(meshSelected.OnTop[0].Color != meshSelected.DepthTested.back().Color);
	}

	TEST_CASE("Picking finds the marker under a point, the nearest when they overlap")
	{
		Fixture fixture;
		const Entity front = fixture.CreateEntity("Front", glm::vec3(0.0f, 0.0f, 2.0f));
		const Entity back = fixture.CreateEntity("Back", glm::vec3(0.0f, 0.0f, -2.0f));
		const Entity side = fixture.CreateEntity("Side", glm::vec3(3.0f, 0.0f, 0.0f));
		// Markers keep their size whatever the scale
		const Entity flat = fixture.CreateEntity("Flat", glm::vec3(-3.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 1.0f));

		CHECK(fixture.Pick(Center) == front.GetId());
		CHECK(fixture.Pick(fixture.Project(glm::vec3(3.0f, 0.1f, 0.0f))) == side.GetId());
		CHECK(fixture.Pick(fixture.Project(glm::vec3(-3.0f, 0.1f, 0.0f))) == flat.GetId());
		CHECK(fixture.Pick(fixture.Project(glm::vec3(0.0f, 3.0f, 0.0f))).IsNil());

		fixture.Context.GetScene().DestroyEntity(fixture.Context.GetScene().FindEntity(front.GetId()));
		CHECK(fixture.Pick(Center) == back.GetId());
	}

	TEST_CASE("Picking hits meshes triangle by triangle, following their transforms")
	{
		Fixture fixture;
		const Entity parent = fixture.CreateEntity("Parent", glm::vec3(-3.0f, 0.0f, 0.0f));
		Entity wide = fixture.CreateMesh("Wide", Cube, glm::vec3(0.0f), glm::vec3(4.0f, 0.2f, 0.2f));
		REQUIRE(fixture.Context.GetScene().SetParent(wide, parent).has_value());
		const Entity ball = fixture.CreateMesh("Ball", Sphere, glm::vec3(3.0f, 0.0f, 0.0f), glm::vec3(2.0f));

		// The wide child spans x from -5 to -1 in the world, at the parent's position
		CHECK(fixture.Pick(fixture.Project(glm::vec3(-4.5f, 0.0f, 0.0f))) == wide.GetId());
		// Turned a quarter around Z, it's tall instead
		wide.GetTransform().Rotation = glm::angleAxis(glm::pi<float>() / 2.0f, glm::vec3(0.0f, 0.0f, 1.0f));
		CHECK(fixture.Pick(fixture.Project(glm::vec3(-4.5f, 0.0f, 0.0f))).IsNil());
		CHECK(fixture.Pick(fixture.Project(glm::vec3(-3.0f, 1.8f, 0.0f))) == wide.GetId());

		// The ball's bounds reach its corners, but its triangles don't
		CHECK(fixture.Pick(fixture.Project(glm::vec3(3.0f, 0.0f, 1.0f))) == ball.GetId());
		CHECK(fixture.Pick(fixture.Project(glm::vec3(3.0f + 0.9f, 0.9f, 1.0f))).IsNil());
		// Until its model loads, it's picked by its marker, which the bounds' corner misses too
		CHECK(fixture.Pick(fixture.Project(glm::vec3(3.0f, 0.0f, 0.0f)), Unloaded) == ball.GetId());
		CHECK(fixture.Pick(fixture.Project(glm::vec3(3.0f, 0.5f, 0.0f)), Unloaded).IsNil());
	}

	TEST_CASE("Picking a mesh the camera is inside finds it, and an empty image finds nothing")
	{
		Fixture fixture;
		const Entity room = fixture.CreateMesh("Room", Cube, glm::vec3(0.0f), glm::vec3(30.0f));

		CHECK(fixture.Pick(Center) == room.GetId());
		CHECK(SceneView::PickEntity(fixture.Context, glm::uvec2(0, 0), Center, Loaded).IsNil());
		CHECK(SceneView::PickEntity(EditorContext(), ImageSize, Center, Loaded).IsNil());
	}

	TEST_CASE("The render view is the editor camera's")
	{
		Fixture fixture;

		const RenderView view = SceneView::GetRenderView(fixture.Context);

		CHECK(glm::all(glm::lessThan(glm::abs(view.GetPosition() - glm::vec3(0.0f, 0.0f, 10.0f)), glm::vec3(1e-4f))));
		CHECK(view.FieldOfView == fixture.Context.GetCamera().GetFieldOfView());
		CHECK(view.NearPlane == EditorCamera::NearPlane);
		CHECK(view.FarPlane == EditorCamera::FarPlane);
	}

}
