#include "Lodestone/Renderer/Frustum.h"

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

namespace Lodestone {

	Frustum::Frustum(const glm::mat4& viewProjection)
	{
		// The planes of the clip volume -w <= x <= w, -w <= y <= w and 0 <= z <= w, in world space (Gribb and
		// Hartmann 2001): each is a sum or difference of the matrix's rows
		const glm::mat4 rows = glm::transpose(viewProjection);
		m_Planes = {
			rows[3] + rows[0],
			rows[3] - rows[0],
			rows[3] + rows[1],
			rows[3] - rows[1],
			rows[2],
			rows[3] - rows[2],
		};
		for (glm::vec4& plane : m_Planes)
		{
			const float length = glm::length(glm::vec3(plane));
			if (length > 0.0f)
				plane /= length;
		}
	}

	bool Frustum::Intersects(const BoundingBox& box) const
	{
		if (box.IsEmpty())
			return false;
		for (const glm::vec4& plane : m_Planes)
		{
			// The box's corner furthest along the plane's normal: if even that is outside, all of the box is
			const glm::vec3 corner(plane.x >= 0.0f ? box.Max.x : box.Min.x, plane.y >= 0.0f ? box.Max.y : box.Min.y,
				plane.z >= 0.0f ? box.Max.z : box.Min.z);
			if (glm::dot(glm::vec3(plane), corner) + plane.w < 0.0f)
				return false;
		}
		return true;
	}

}
