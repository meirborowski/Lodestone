#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

namespace Lodestone {

	// Euler angles, as the inspector and scripts show rotations: degrees around X (pitch), then Y (yaw), then Z (roll),
	// each around the fixed world axes - so the rotation is roll * yaw * pitch, the order glm::quat(radians) uses
	glm::quat QuaternionFromEuler(const glm::vec3& degrees);

	// The inverse of QuaternionFromEuler(): pitch and roll from -180 to 180 degrees, yaw from -90 to 90. Where yaw is
	// at either end, pitch and roll turn around the same axis, and the whole turn is given as pitch. Precise there
	// too, unlike glm::eulerAngles(), whose yaw comes from an asin that's steep near 90 degrees
	glm::vec3 EulerFromQuaternion(const glm::quat& rotation);

}
