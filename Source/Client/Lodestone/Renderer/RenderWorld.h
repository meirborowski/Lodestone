#pragma once

#include "Lodestone/Core/UUID.h"
#include "Lodestone/Scene/RenderComponents.h"

#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace Lodestone {

	class Scene;

	// Where a view is seen from: a camera's placement and lens. The renderer makes the projection from it and the
	// image's aspect ratio
	struct RenderView
	{
		// From world space to the camera's, which looks down -Z with +Y up. Rigid: no scale
		glm::mat4 View{1.0f};
		// Vertical, in radians
		float FieldOfView = 1.04719755f;
		float NearPlane = 0.1f;
		float FarPlane = 500.0f;

		glm::vec3 GetPosition() const;
		// A perspective projection for an aspect ratio (width over height), into NVRHI's clip space (+Y up) with
		// reversed depth: 1 at the near plane, 0 at the far plane
		glm::mat4 GetProjection(float aspectRatio) const;
	};

	// A mesh to draw
	struct RenderMesh
	{
		UUID Entity;
		UUID Model;
		uint32_t Mesh = 0;
		glm::mat4 World{1.0f};
		bool CastShadows = true;
	};

	struct RenderDirectionalLight
	{
		UUID Entity;
		// The direction the light travels, unit length
		glm::vec3 Direction{0.0f, -1.0f, 0.0f};
		// Linear RGB times the intensity
		glm::vec3 Color{1.0f};
		bool CastShadows = true;
	};

	// A point or spot light
	struct RenderPunctualLight
	{
		UUID Entity;
		glm::vec3 Position{0.0f};
		// Linear RGB times the intensity
		glm::vec3 Color{1.0f};
		float Range = 10.0f;
		bool Spot = false;
		// Spot lights only: the direction the cone points, unit length, and the cosines of its angles
		glm::vec3 Direction{0.0f, 0.0f, -1.0f};
		float CosInnerAngle = 1.0f;
		float CosOuterAngle = 0.0f;
	};

	// What a scene looks like at a moment, for the renderer: its meshes, lights, environment, settings and camera,
	// with world transforms
	struct RenderWorld
	{
		std::vector<RenderMesh> Meshes;
		std::vector<RenderDirectionalLight> DirectionalLights;
		std::vector<RenderPunctualLight> PunctualLights;
		EnvironmentComponent Environment;
		RenderSettingsComponent Settings;
		// The scene's primary camera, if it has one
		std::optional<RenderView> Camera;
	};

	// Collects what the renderer draws from a scene. Transforms are interpolated between the start and the end of the
	// latest simulation tick by the factor (Simulation::GetInterpolationFactor()); 1 takes the current transforms.
	//
	// Entities are visited in hierarchy order - roots in order, each followed by its children - so the result doesn't
	// depend on how the ECS stores them. A scene should have at most one environment, one render settings component
	// and one primary camera; with several, the first wins. Directional lights, spot lights and cameras whose world
	// transform squashes their forward direction to nothing are left out, having no direction
	RenderWorld ExtractRenderWorld(const Scene& scene, float interpolationFactor = 1.0f);

	// The matrix that transforms normals for a world matrix: the inverse transpose of its upper 3x3, up to scale (the
	// cofactor matrix, with the determinant's sign), which also works for matrices that flatten a mesh
	glm::mat3 GetNormalMatrix(const glm::mat4& world);

}
