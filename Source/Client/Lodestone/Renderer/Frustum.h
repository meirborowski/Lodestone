#pragma once

#include "Lodestone/Core/BoundingBox.h"

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <array>

namespace Lodestone {

	// The volume a view-projection matrix sees, as six planes, for culling what's outside it. Works for perspective
	// and orthographic projections into NVRHI's clip space, with depth from 0 to 1 either way round
	class Frustum
	{
	public:
		explicit Frustum(const glm::mat4& viewProjection);

		// Whether any of a world-space box may be inside. Conservative: boxes near the frustum's corners can pass
		// without being inside
		bool Intersects(const BoundingBox& box) const;

	private:
		// Each plane's normal (xyz) points inward; points inside have dot(plane.xyz, point) + plane.w >= 0
		std::array<glm::vec4, 6> m_Planes{};
	};

}
