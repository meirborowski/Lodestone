#include "Lodestone/Editor/SceneView.h"

#include "Lodestone/Scene/Entity.h"

#include <doctest/doctest.h>
#include <glm/ext/scalar_constants.hpp>

namespace Lodestone {

	namespace {

		constexpr glm::uvec2 ImageSize{800, 600};
		constexpr glm::vec2 Center(400.0f, 300.0f);

		// An editor looking down -Z at the origin from 10 m away
		struct Fixture
		{
			Fixture() { Context.GetCamera().LookAt(glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f)); }

			Entity CreateCube(const char* name, glm::vec3 position, glm::vec3 scale = glm::vec3(1.0f))
			{
				Entity entity = Context.GetScene().CreateEntity(name);
				entity.GetTransform().Position = position;
				entity.GetTransform().Scale = scale;
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

			EditorContext Context;
		};

	}

	TEST_CASE("The scene view draws a cube for every entity at its world transform")
	{
		Fixture fixture;
		const Entity parent = fixture.CreateCube("Parent", glm::vec3(1.0f, 0.0f, 0.0f));
		const Entity child = fixture.CreateCube("Child", glm::vec3(0.0f, 2.0f, 0.0f));
		REQUIRE(fixture.Context.GetScene().SetParent(child, parent).has_value());

		const std::vector<DebugCube> cubes = SceneView::BuildCubes(fixture.Context);

		REQUIRE(cubes.size() == 2);
		bool foundChild = false;
		for (const DebugCube& cube : cubes)
			foundChild = foundChild || glm::vec3(cube.World[3]) == glm::vec3(1.0f, 2.0f, 0.0f);
		CHECK(foundChild);
	}

	TEST_CASE("The scene view highlights the selection, and keeps each entity's color")
	{
		Fixture fixture;
		const Entity a = fixture.CreateCube("A", glm::vec3(0.0f));
		fixture.CreateCube("B", glm::vec3(2.0f, 0.0f, 0.0f));
		const std::vector<DebugCube> before = SceneView::BuildCubes(fixture.Context);

		fixture.Context.Select(a.GetId());
		const std::vector<DebugCube> selected = SceneView::BuildCubes(fixture.Context);

		REQUIRE(selected.size() == 2);
		int changed = 0;
		for (size_t index = 0; index < selected.size(); ++index)
			changed += selected[index].Color != before[index].Color ? 1 : 0;
		CHECK(changed == 1);
		CHECK(SceneView::BuildCubes(fixture.Context)[0].Color == selected[0].Color);
	}

	TEST_CASE("Picking finds the entity under a point, the nearest when they overlap")
	{
		Fixture fixture;
		const Entity front = fixture.CreateCube("Front", glm::vec3(0.0f, 0.0f, 2.0f));
		const Entity back = fixture.CreateCube("Back", glm::vec3(0.0f, 0.0f, -2.0f));
		const Entity side = fixture.CreateCube("Side", glm::vec3(3.0f, 0.0f, 0.0f));

		CHECK(SceneView::PickEntity(fixture.Context, ImageSize, Center) == front.GetId());
		CHECK(SceneView::PickEntity(fixture.Context, ImageSize, fixture.Project(glm::vec3(3.0f, 0.0f, 0.5f))) ==
			side.GetId());
		// Nothing above them
		CHECK(SceneView::PickEntity(fixture.Context, ImageSize, fixture.Project(glm::vec3(0.0f, 3.0f, 0.0f))).IsNil());

		fixture.Context.GetScene().DestroyEntity(fixture.Context.GetScene().FindEntity(front.GetId()));
		CHECK(SceneView::PickEntity(fixture.Context, ImageSize, Center) == back.GetId());
	}

	TEST_CASE("Picking follows rotation, scale and the hierarchy")
	{
		Fixture fixture;
		const Entity parent = fixture.CreateCube("Parent", glm::vec3(-3.0f, 0.0f, 0.0f));
		Entity wide = fixture.CreateCube("Wide", glm::vec3(0.0f), glm::vec3(4.0f, 0.2f, 0.2f));
		REQUIRE(fixture.Context.GetScene().SetParent(wide, parent).has_value());
		// The wide child spans x from -5 to -1 in the world, at the parent's position
		CHECK(SceneView::PickEntity(fixture.Context, ImageSize, fixture.Project(glm::vec3(-4.5f, 0.0f, 0.0f))) ==
			wide.GetId());

		// Turned a quarter around Z, it's tall instead
		wide.GetTransform().Rotation = glm::angleAxis(glm::pi<float>() / 2.0f, glm::vec3(0.0f, 0.0f, 1.0f));
		CHECK(SceneView::PickEntity(fixture.Context, ImageSize, fixture.Project(glm::vec3(-4.5f, 0.0f, 0.0f))).IsNil());
		CHECK(SceneView::PickEntity(fixture.Context, ImageSize, fixture.Project(glm::vec3(-3.0f, 1.8f, 0.0f))) ==
			wide.GetId());
	}

	TEST_CASE("Picking skips entities scaled flat, and works in an empty or zero-sized image")
	{
		Fixture fixture;
		fixture.CreateCube("Flat", glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 1.0f));

		CHECK(SceneView::PickEntity(fixture.Context, ImageSize, Center).IsNil());
		CHECK(SceneView::PickEntity(fixture.Context, glm::uvec2(0, 0), Center).IsNil());
		CHECK(SceneView::PickEntity(EditorContext(), ImageSize, Center).IsNil());
	}

	TEST_CASE("Picking a cube the camera is inside finds it")
	{
		Fixture fixture;
		const Entity room = fixture.CreateCube("Room", glm::vec3(0.0f), glm::vec3(30.0f));

		CHECK(SceneView::PickEntity(fixture.Context, ImageSize, Center) == room.GetId());
	}

}
