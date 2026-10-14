#include "Lodestone/Core/Rotation.h"

#include <glm/mat3x3.hpp>
#include <glm/trigonometric.hpp>

#include <cmath>
#include <limits>

namespace Lodestone {

	namespace {

		// Below this cosine, yaw is 90 degrees as far as a float rotation can tell, and roll is undefined
		constexpr float GimbalLockCosine = 16.0f * std::numeric_limits<float>::epsilon();

	}

	glm::quat QuaternionFromEuler(const glm::vec3& degrees)
	{
		return {glm::radians(degrees)};
	}

	glm::vec3 EulerFromQuaternion(const glm::quat& rotation)
	{
		// R = Rz(roll) * Ry(yaw) * Rx(pitch). glm indexes matrices [column][row], so R(row, column) is m[column][row]
		const glm::mat3 m = glm::mat3_cast(glm::normalize(rotation));

		// The first column is (cos yaw cos roll, cos yaw sin roll, -sin yaw): yaw's cosine from its length keeps yaw
		// precise where its sine is near 1
		const float cosYaw = std::hypot(m[0][0], m[0][1]);
		const float yaw = std::atan2(-m[0][2], cosYaw);
		const float roll = cosYaw > GimbalLockCosine ? std::atan2(m[0][1], m[0][0]) : 0.0f;

		// Undoing roll leaves Rz(-roll) * R = Ry(yaw) * Rx(pitch), whose middle row is (0, cos pitch, -sin pitch).
		// Unlike the last row, it's well conditioned at every yaw, and pitch made from it agrees with roll: near
		// gimbal lock, where roll is imprecise, pitch makes up for it
		const float sinRoll = std::sin(roll);
		const float cosRoll = std::cos(roll);
		const float pitch = std::atan2(sinRoll * m[2][0] - cosRoll * m[2][1], cosRoll * m[1][1] - sinRoll * m[1][0]);

		// Adding zero turns -0 into 0
		return glm::degrees(glm::vec3(pitch, yaw, roll)) + glm::vec3(0.0f);
	}

}
