#include "Lodestone/Editor/EditorCamera.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

namespace Lodestone {

	namespace {

		// Just short of straight up or down, where the view's up direction becomes undefined
		constexpr float MaxPitch = 1.55f;
		constexpr float NearPlane = 0.05f;
		constexpr float FarPlane = 5000.0f;
		constexpr glm::vec3 WorldUp{0.0f, 1.0f, 0.0f};

	}

	glm::vec3 EditorCamera::GetPosition() const
	{
		const glm::vec3 direction(
			std::cos(m_Pitch) * std::sin(m_Yaw), std::sin(m_Pitch), std::cos(m_Pitch) * std::cos(m_Yaw));
		return m_Target + direction * m_Distance;
	}

	void EditorCamera::LookAt(const glm::vec3& position, const glm::vec3& target)
	{
		const glm::vec3 offset = position - target;
		const float distance = glm::length(offset);
		if (!(distance > 0.0f) || !std::isfinite(distance))
			return;
		m_Target = target;
		m_Distance = std::clamp(distance, MinDistance, MaxDistance);
		m_Pitch = std::clamp(std::asin(std::clamp(offset.y / distance, -1.0f, 1.0f)), -MaxPitch, MaxPitch);
		m_Yaw = std::atan2(offset.x, offset.z);
	}

	void EditorCamera::Orbit(float yawDelta, float pitchDelta)
	{
		m_Yaw = std::remainder(m_Yaw + yawDelta, 2.0f * glm::pi<float>());
		m_Pitch = std::clamp(m_Pitch + pitchDelta, -MaxPitch, MaxPitch);
	}

	void EditorCamera::Pan(float right, float up)
	{
		const glm::vec3 forward = glm::normalize(m_Target - GetPosition());
		const glm::vec3 rightAxis = glm::normalize(glm::cross(forward, WorldUp));
		const glm::vec3 upAxis = glm::cross(rightAxis, forward);
		m_Target += rightAxis * right + upAxis * up;
	}

	void EditorCamera::Zoom(float factor)
	{
		if (factor > 0.0f && std::isfinite(factor))
			m_Distance = std::clamp(m_Distance * factor, MinDistance, MaxDistance);
	}

	void EditorCamera::SetFieldOfView(float radians)
	{
		m_FieldOfView = std::clamp(radians, glm::radians(10.0f), glm::radians(150.0f));
	}

	glm::mat4 EditorCamera::GetView() const
	{
		return glm::lookAtRH(GetPosition(), m_Target, WorldUp);
	}

	glm::mat4 EditorCamera::GetProjection(float aspectRatio) const
	{
		return glm::perspectiveRH_ZO(m_FieldOfView, std::max(aspectRatio, 0.01f), NearPlane, FarPlane);
	}

}
