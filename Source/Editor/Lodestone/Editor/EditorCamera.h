#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace Lodestone {

	// The editor's viewport camera: it orbits a target point. Angles are in radians; yaw turns around +Y, pitch tilts
	// up from the horizon
	class EditorCamera
	{
	public:
		static constexpr float MinDistance = 0.1f;
		static constexpr float MaxDistance = 10000.0f;

		glm::vec3 GetTarget() const { return m_Target; }
		float GetDistance() const { return m_Distance; }
		float GetYaw() const { return m_Yaw; }
		float GetPitch() const { return m_Pitch; }
		float GetFieldOfView() const { return m_FieldOfView; }
		glm::vec3 GetPosition() const;

		// Places the camera at a position looking at a target. The two must differ
		void LookAt(const glm::vec3& position, const glm::vec3& target);
		void Orbit(float yawDelta, float pitchDelta);
		// Moves the target and the camera together, in the camera's view plane
		void Pan(float right, float up);
		// Multiplies the distance to the target
		void Zoom(float factor);
		void SetFieldOfView(float radians);

		glm::mat4 GetView() const;
		// A perspective projection for NVRHI's clip space (+Y up, depth from 0 to 1)
		glm::mat4 GetProjection(float aspectRatio) const;

	private:
		glm::vec3 m_Target{0.0f};
		float m_Distance = 12.0f;
		float m_Yaw = 0.785398f;
		float m_Pitch = 0.523599f;
		float m_FieldOfView = 1.047198f;
	};

}
