#include "Lodestone/Core/BoundingBox.h"

#include <doctest/doctest.h>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/geometric.hpp>

namespace Lodestone {

	TEST_CASE("A bounding box starts empty and grows around what's added")
	{
		BoundingBox box;
		CHECK(box.IsEmpty());

		box.Add(glm::vec3(1.0f, 2.0f, 3.0f));
		CHECK_FALSE(box.IsEmpty());
		CHECK(box.Min == glm::vec3(1.0f, 2.0f, 3.0f));
		CHECK(box.Max == glm::vec3(1.0f, 2.0f, 3.0f));

		box.Add(glm::vec3(-1.0f, 4.0f, 0.0f));
		CHECK(box.Min == glm::vec3(-1.0f, 2.0f, 0.0f));
		CHECK(box.Max == glm::vec3(1.0f, 4.0f, 3.0f));
		CHECK(box.GetCenter() == glm::vec3(0.0f, 3.0f, 1.5f));
		CHECK(box.GetExtent() == glm::vec3(2.0f, 2.0f, 3.0f));

		// An empty box adds nothing
		BoundingBox other = box;
		other.Add(BoundingBox());
		CHECK(other == box);
	}

	TEST_CASE("A transformed bounding box contains every transformed corner")
	{
		const BoundingBox box{.Min = glm::vec3(-1.0f), .Max = glm::vec3(1.0f)};
		const glm::mat4 matrix = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f)) *
			glm::rotate(glm::mat4(1.0f), glm::pi<float>() / 4.0f, glm::vec3(0.0f, 1.0f, 0.0f));

		const BoundingBox transformed = box.Transform(matrix);

		const float diagonal = std::sqrt(2.0f);
		CHECK(transformed.Min.x == doctest::Approx(10.0f - diagonal));
		CHECK(transformed.Max.x == doctest::Approx(10.0f + diagonal));
		CHECK(transformed.Min.y == doctest::Approx(-1.0f));
		CHECK(BoundingBox().Transform(matrix).IsEmpty());
	}

	TEST_CASE("Rays enter bounding boxes at the nearest face, or miss them")
	{
		const BoundingBox box{.Min = glm::vec3(-1.0f), .Max = glm::vec3(1.0f)};

		const auto hit = box.Intersect(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, -1.0f));
		REQUIRE(hit.has_value());
		CHECK(*hit == doctest::Approx(4.0f));
		// From inside: at once
		CHECK(box.Intersect(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)) == 0.0f);
		CHECK_FALSE(box.Intersect(glm::vec3(0.0f, 3.0f, 5.0f), glm::vec3(0.0f, 0.0f, -1.0f)).has_value());
		// Pointing away
		CHECK_FALSE(box.Intersect(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, 1.0f)).has_value());
		CHECK_FALSE(BoundingBox().Intersect(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)).has_value());
	}

}
