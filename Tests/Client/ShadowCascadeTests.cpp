#include "Lodestone/Renderer/Frustum.h"
#include "Lodestone/Renderer/ShadowCascades.h"

#include <doctest/doctest.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

#include <array>
#include <cmath>
#include <vector>

namespace Lodestone {

	namespace {

		constexpr float AspectRatio = 16.0f / 9.0f;
		// normalize(-0.4, -1, -0.3)
		constexpr glm::vec3 LightDirection{-0.36380f, -0.90951f, -0.27285f};

		RenderView MakeView(const glm::vec3& position, const glm::vec3& target)
		{
			return {
				.View = glm::lookAtRH(position, target, glm::vec3(0.0f, 1.0f, 0.0f)),
				.NearPlane = 0.1f,
				.FarPlane = 500.0f,
			};
		}

		BoundingBox MakeBox(const glm::vec3& center, float halfSize)
		{
			return {.Min = center - halfSize, .Max = center + halfSize};
		}

		// The corners of the slice of a view's frustum between two depths, in world space
		std::array<glm::vec3, 8> GetSliceCorners(const RenderView& view, float nearDepth, float farDepth)
		{
			const glm::mat4 toWorld = glm::inverse(view.View);
			const float tangent = std::tan(view.FieldOfView * 0.5f);
			std::array<glm::vec3, 8> corners{};
			size_t index = 0;
			for (const float depth : {nearDepth, farDepth})
			{
				for (const float x : {-1.0f, 1.0f})
				{
					for (const float y : {-1.0f, 1.0f})
					{
						const glm::vec4 local(x * tangent * AspectRatio * depth, y * tangent * depth, -depth, 1.0f);
						corners[index++] = glm::vec3(toWorld * local);
					}
				}
			}
			return corners;
		}

	}

	TEST_CASE("A frustum keeps boxes inside it and culls those outside, for perspective and orthographic views")
	{
		const RenderView view = MakeView(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
		const Frustum perspective(view.GetProjection(1.0f) * view.View);

		CHECK(perspective.Intersects(MakeBox(glm::vec3(0.0f, 0.0f, -10.0f), 1.0f)));
		CHECK_FALSE(perspective.Intersects(MakeBox(glm::vec3(0.0f, 0.0f, 10.0f), 1.0f)));
		CHECK_FALSE(perspective.Intersects(MakeBox(glm::vec3(-30.0f, 0.0f, -10.0f), 1.0f)));
		CHECK_FALSE(perspective.Intersects(MakeBox(glm::vec3(0.0f, 0.0f, -600.0f), 1.0f)));
		// Straddling the near plane and an edge
		CHECK(perspective.Intersects(MakeBox(glm::vec3(0.0f), 1.0f)));
		// At 10 meters, a 60-degree view reaches 5.8 meters to each side
		CHECK(perspective.Intersects(MakeBox(glm::vec3(-6.0f, 0.0f, -10.0f), 1.0f)));
		CHECK_FALSE(perspective.Intersects(BoundingBox()));

		const Frustum orthographic(glm::orthoRH_ZO(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 10.0f));
		CHECK(orthographic.Intersects(MakeBox(glm::vec3(0.5f, 0.5f, -5.0f), 0.1f)));
		CHECK_FALSE(orthographic.Intersects(MakeBox(glm::vec3(3.0f, 0.0f, -5.0f), 0.1f)));
		CHECK_FALSE(orthographic.Intersects(MakeBox(glm::vec3(0.0f, 0.0f, -20.0f), 0.1f)));
	}

	TEST_CASE("Shadow cascades split the view out to the shadow distance, each slice inside its cascade")
	{
		const RenderView view = MakeView(glm::vec3(2.0f, 3.0f, 8.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		const ShadowCascadeSettings settings{.CascadeCount = 4, .MapSize = 1024, .ShadowDistance = 40.0f};

		const std::vector<ShadowCascade> cascades =
			ComputeShadowCascades(LightDirection, view, AspectRatio, settings, MakeBox(glm::vec3(0.0f), 2.0f));

		REQUIRE(cascades.size() == 4);
		float sliceStart = view.NearPlane;
		for (const ShadowCascade& cascade : cascades)
		{
			CAPTURE(cascade.SplitDepth);
			CHECK(cascade.SplitDepth > sliceStart);
			CHECK(cascade.TexelSize > 0.0f);
			CHECK(cascade.DepthRange > 0.0f);
			for (const glm::vec3& corner : GetSliceCorners(view, sliceStart, cascade.SplitDepth))
			{
				const glm::vec4 clip = cascade.ViewProjection * glm::vec4(corner, 1.0f);
				CHECK(std::abs(clip.x) <= 1.0f);
				CHECK(std::abs(clip.y) <= 1.0f);
				CHECK(clip.z >= 0.0f);
				CHECK(clip.z <= 1.0f);
			}
			sliceStart = cascade.SplitDepth;
		}
		CHECK(cascades.back().SplitDepth == doctest::Approx(40.0f));
		// Nearer cascades have finer texels
		CHECK(cascades[0].TexelSize < cascades[3].TexelSize);
	}

	TEST_CASE("Shadow cascades reach back to casters toward the light, wherever they are")
	{
		const RenderView view = MakeView(glm::vec3(0.0f, 1.0f, 5.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		// A caster far up the light's path, well outside the view
		const glm::vec3 casterCenter = -LightDirection * 80.0f;

		const std::vector<ShadowCascade> cascades = ComputeShadowCascades(
			LightDirection, view, AspectRatio, {.ShadowDistance = 30.0f}, MakeBox(casterCenter, 1.0f));

		REQUIRE_FALSE(cascades.empty());
		for (const ShadowCascade& cascade : cascades)
		{
			const glm::vec4 clip = cascade.ViewProjection * glm::vec4(casterCenter, 1.0f);
			CHECK(clip.z >= 0.0f);
			CHECK(clip.z < 1.0f);
		}
	}

	TEST_CASE("Shadow cascades keep their size as the camera turns, and move in whole texels")
	{
		const ShadowCascadeSettings settings{.CascadeCount = 2, .MapSize = 512, .ShadowDistance = 20.0f};
		const RenderView ahead = MakeView(glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, 2.0f, -1.0f));
		const RenderView turned = MakeView(glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(1.0f, 1.5f, -0.2f));
		const RenderView moved = MakeView(glm::vec3(0.013f, 2.0f, 0.0f), glm::vec3(0.013f, 2.0f, -1.0f));

		const auto first = ComputeShadowCascades(LightDirection, ahead, AspectRatio, settings, BoundingBox());
		const auto second = ComputeShadowCascades(LightDirection, turned, AspectRatio, settings, BoundingBox());
		const auto third = ComputeShadowCascades(LightDirection, moved, AspectRatio, settings, BoundingBox());

		REQUIRE(first.size() == 2);
		REQUIRE(second.size() == 2);
		REQUIRE(third.size() == 2);
		for (size_t i = 0; i < first.size(); ++i)
		{
			CHECK(second[i].TexelSize == first[i].TexelSize);
			// The same world point lands a whole number of texels away, so texels don't swim across edges
			const glm::vec4 before = first[i].ViewProjection * glm::vec4(0.0f, 0.0f, -3.0f, 1.0f);
			const glm::vec4 after = third[i].ViewProjection * glm::vec4(0.0f, 0.0f, -3.0f, 1.0f);
			const glm::vec2 shift =
				(glm::vec2(after) - glm::vec2(before)) * (static_cast<float>(settings.MapSize) * 0.5f);
			CHECK(std::abs(shift.x - std::round(shift.x)) < 1e-2f);
			CHECK(std::abs(shift.y - std::round(shift.y)) < 1e-2f);
		}
	}

	TEST_CASE("No cascades when the view starts beyond the shadow distance")
	{
		RenderView view = MakeView(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
		view.NearPlane = 50.0f;

		CHECK(
			ComputeShadowCascades(LightDirection, view, AspectRatio, {.ShadowDistance = 40.0f}, BoundingBox()).empty());
	}

}
