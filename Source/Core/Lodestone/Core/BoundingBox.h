#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <limits>
#include <optional>

namespace Lodestone {

	// An axis-aligned box. A default box is empty: it contains nothing, until points are added
	struct BoundingBox
	{
		glm::vec3 Min{std::numeric_limits<float>::max()};
		glm::vec3 Max{std::numeric_limits<float>::lowest()};

		bool IsEmpty() const { return Min.x > Max.x || Min.y > Max.y || Min.z > Max.z; }
		glm::vec3 GetCenter() const { return (Min + Max) * 0.5f; }
		glm::vec3 GetExtent() const { return Max - Min; }

		void Add(const glm::vec3& point);
		void Add(const BoundingBox& box);

		// The box around this one once transformed (still axis-aligned, so it may grow)
		BoundingBox Transform(const glm::mat4& matrix) const;

		// Where a ray enters the box, as a distance along its direction (0 when it starts inside), if it hits it
		std::optional<float> Intersect(const glm::vec3& origin, const glm::vec3& direction) const;

		bool operator==(const BoundingBox& other) const = default;
	};

}
