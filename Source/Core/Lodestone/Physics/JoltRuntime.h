#pragma once

// Internal to the physics module: Jolt is linked privately by LodestoneCore, so only its sources include this

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <Jolt/Jolt.h>
#include <Jolt/Math/Quat.h>
#include <Jolt/Math/Vec3.h>

namespace Lodestone {

	// Sets up Jolt for the process, the first time it's called: its allocator, the factory and type registry its
	// shapes need, and its trace and assert hooks, which report through the engine's log and assert handler. It stays
	// set up until the process exits. Thread-safe
	void InitializeJoltRuntime();

	inline JPH::Vec3 ToJolt(const glm::vec3& value)
	{
		return {value.x, value.y, value.z};
	}

	inline JPH::Quat ToJolt(const glm::quat& value)
	{
		return {value.x, value.y, value.z, value.w};
	}

	inline glm::vec3 ToGlm(JPH::Vec3Arg value)
	{
		return {value.GetX(), value.GetY(), value.GetZ()};
	}

	inline glm::quat ToGlm(JPH::QuatArg value)
	{
		return glm::quat::wxyz(value.GetW(), value.GetX(), value.GetY(), value.GetZ());
	}

}
