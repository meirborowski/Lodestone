#include "Lodestone/Core/BoundingBox.h"

#include <glm/common.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace Lodestone {

	void BoundingBox::Add(const glm::vec3& point)
	{
		Min = glm::min(Min, point);
		Max = glm::max(Max, point);
	}

	void BoundingBox::Add(const BoundingBox& box)
	{
		if (box.IsEmpty())
			return;
		Min = glm::min(Min, box.Min);
		Max = glm::max(Max, box.Max);
	}

	BoundingBox BoundingBox::Transform(const glm::mat4& matrix) const
	{
		if (IsEmpty())
			return {};
		BoundingBox result;
		for (int corner = 0; corner < 8; ++corner)
		{
			const glm::vec3 point((corner & 1) != 0 ? Max.x : Min.x, (corner & 2) != 0 ? Max.y : Min.y,
				(corner & 4) != 0 ? Max.z : Min.z);
			result.Add(glm::vec3(matrix * glm::vec4(point, 1.0f)));
		}
		return result;
	}

	std::optional<float> BoundingBox::Intersect(const glm::vec3& origin, const glm::vec3& direction) const
	{
		if (IsEmpty())
			return std::nullopt;
		float nearest = 0.0f;
		float farthest = std::numeric_limits<float>::max();
		for (int axis = 0; axis < 3; ++axis)
		{
			if (std::abs(direction[axis]) < 1e-12f)
			{
				// Parallel to this pair of faces: inside the slab, or a miss
				if (origin[axis] < Min[axis] || origin[axis] > Max[axis])
					return std::nullopt;
				continue;
			}
			float enter = (Min[axis] - origin[axis]) / direction[axis];
			float exit = (Max[axis] - origin[axis]) / direction[axis];
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
