#include "Lodestone/Core/Rotation.h"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace Lodestone {

	namespace {

		// Whether two rotations turn the axes the same way, to within a tolerance in world units
		bool SameRotation(const glm::quat& a, const glm::quat& b, float tolerance = 1e-5f)
		{
			constexpr std::array axes{
				glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)};
			return std::ranges::all_of(
				axes, [&](const glm::vec3& axis) { return glm::distance(a * axis, b * axis) <= tolerance; });
		}

		bool Near(const glm::vec3& a, const glm::vec3& b, float tolerance)
		{
			return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(tolerance)));
		}

	}

	TEST_CASE("Euler angles turn around X, then Y, then Z, around the world's axes")
	{
		const glm::quat pitch = glm::angleAxis(glm::radians(30.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		const glm::quat yaw = glm::angleAxis(glm::radians(40.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		const glm::quat roll = glm::angleAxis(glm::radians(50.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		CHECK(SameRotation(QuaternionFromEuler(glm::vec3(30.0f, 40.0f, 50.0f)), roll * yaw * pitch));
	}

	TEST_CASE("Euler angles come back as they went in")
	{
		constexpr std::array pitches{-170.0f, -90.0f, -30.0f, 0.0f, 45.0f, 120.0f, 179.0f};
		constexpr std::array yaws{-89.0f, -60.0f, -1.0f, 0.0f, 30.0f, 89.5f};
		constexpr std::array rolls{-135.0f, -45.0f, 0.0f, 10.0f, 90.0f, 170.0f};
		for (const float pitch : pitches)
			for (const float yaw : yaws)
				for (const float roll : rolls)
				{
					const glm::vec3 degrees(pitch, yaw, roll);
					const glm::vec3 result = EulerFromQuaternion(QuaternionFromEuler(degrees));
					CAPTURE(degrees.x);
					CAPTURE(degrees.y);
					CAPTURE(degrees.z);
					CHECK(Near(result, degrees, 2e-3f));
				}
	}

	TEST_CASE("Quarter turns give exact Euler angles")
	{
		const glm::vec3 x(1.0f, 0.0f, 0.0f);
		const glm::vec3 y(0.0f, 1.0f, 0.0f);
		const glm::vec3 z(0.0f, 0.0f, 1.0f);
		CHECK(Near(EulerFromQuaternion(glm::angleAxis(glm::radians(90.0f), x)), glm::vec3(90.0f, 0.0f, 0.0f), 1e-4f));
		CHECK(Near(EulerFromQuaternion(glm::angleAxis(glm::radians(90.0f), z)), glm::vec3(0.0f, 0.0f, 90.0f), 1e-4f));
		// Where glm::eulerAngles() is off by hundredths of a degree
		CHECK(Near(EulerFromQuaternion(glm::angleAxis(glm::radians(90.0f), y)), glm::vec3(0.0f, 90.0f, 0.0f), 1e-4f));
		CHECK(Near(EulerFromQuaternion(glm::angleAxis(glm::radians(-90.0f), y)), glm::vec3(0.0f, -90.0f, 0.0f), 1e-4f));
	}

	TEST_CASE("At a yaw of 90 degrees, Euler angles give the whole turn as pitch, and still make the same rotation")
	{
		for (const float yaw : {90.0f, -90.0f})
			for (const float pitch : {-120.0f, 0.0f, 35.0f})
				for (const float roll : {-60.0f, 0.0f, 25.0f})
				{
					const glm::quat rotation = QuaternionFromEuler(glm::vec3(pitch, yaw, roll));
					const glm::vec3 result = EulerFromQuaternion(rotation);
					CAPTURE(pitch);
					CAPTURE(yaw);
					CAPTURE(roll);
					CHECK(std::abs(result.y - yaw) < 1e-3f);
					CHECK(result.z == 0.0f);
					CHECK(SameRotation(QuaternionFromEuler(result), rotation));
				}
	}

	TEST_CASE("Near a yaw of 90 degrees, Euler angles still make the same rotation")
	{
		for (const float offset : {1e-5f, 1e-4f, 1e-3f, 1e-2f})
			for (const float pitch : {-120.0f, 0.0f, 35.0f})
				for (const float roll : {-60.0f, 0.0f, 25.0f})
				{
					const glm::quat rotation = QuaternionFromEuler(glm::vec3(pitch, 90.0f - offset, roll));
					CAPTURE(offset);
					CAPTURE(pitch);
					CAPTURE(roll);
					CHECK(SameRotation(QuaternionFromEuler(EulerFromQuaternion(rotation)), rotation));
				}
	}

	TEST_CASE("Euler angles of rotations that aren't normalized, and without negative zeros")
	{
		const glm::quat turn = glm::angleAxis(glm::radians(30.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		CHECK(Near(EulerFromQuaternion(turn * 3.0f), glm::vec3(30.0f, 0.0f, 0.0f), 1e-4f));

		const glm::vec3 identity = EulerFromQuaternion(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
		CHECK(identity == glm::vec3(0.0f));
		CHECK_FALSE(std::signbit(identity.x));
		CHECK_FALSE(std::signbit(identity.y));
		CHECK_FALSE(std::signbit(identity.z));
	}

}
