#pragma once

#include "Lodestone/Core/UUID.h"

#include <glm/vec3.hpp>

#include <cstdint>

// The components that say what entities look like and how a scene is lit. They're data only - LodestoneClient's
// renderer draws them - so the simulation, the dedicated server and scripts can read and change them like any other
// component. Each is registered in RegisterRenderComponents(). Lights and cameras face the entity's forward direction,
// -Z, as in glTF

namespace Lodestone {

	class ComponentRegistry;

	// Draws a mesh of a model asset
	struct MeshRendererComponent
	{
		// The model asset
		UUID Model;
		// Which of the model's meshes
		uint32_t Mesh = 0;
		bool CastShadows = true;
	};

	// A camera the game can render from
	struct CameraComponent
	{
		// Vertical, in degrees
		float FieldOfView = 60.0f;
		float NearPlane = 0.1f;
		float FarPlane = 500.0f;
		// The camera the game renders from, when the scene has several
		bool Primary = true;
	};

	// Light from far away, like the sun, in one direction
	struct DirectionalLightComponent
	{
		// Linear RGB
		glm::vec3 Color{1.0f};
		float Intensity = 3.0f;
		bool CastShadows = true;
	};

	// Light from a point, in every direction, fading to nothing at its range
	struct PointLightComponent
	{
		glm::vec3 Color{1.0f};
		float Intensity = 10.0f;
		float Range = 10.0f;
	};

	// Light from a point, in a cone
	struct SpotLightComponent
	{
		glm::vec3 Color{1.0f};
		float Intensity = 10.0f;
		float Range = 10.0f;
		// Degrees from the cone's axis: full brightness inside the inner angle, none outside the outer
		float InnerAngle = 25.0f;
		float OuterAngle = 35.0f;
	};

	// Image-based lighting and the sky, from an HDR environment map. One per scene
	struct EnvironmentComponent
	{
		// The environment asset (an .hdr file); nil for a plain grey sky
		UUID Environment;
		float Intensity = 1.0f;
		// Turns the environment around the vertical axis, in degrees
		float Rotation = 0.0f;
		bool ShowSky = true;
	};

	enum class Tonemapper : uint32_t
	{
		// ACES filmic: rich contrast, desaturates highlights
		Aces = 0,
		// Khronos PBR Neutral: keeps material colors true
		Neutral = 1,
		// Clamped linear color
		None = 2,
	};

	// How a scene is rendered: exposure, tonemapping, ambient occlusion and shadows. One per scene
	struct RenderSettingsComponent
	{
		// In stops: +1 doubles the brightness
		float Exposure = 0.0f;
		// A Tonemapper
		uint32_t Tonemapper = 0;
		bool AmbientOcclusion = true;
		// In meters
		float AmbientOcclusionRadius = 0.5f;
		float AmbientOcclusionIntensity = 1.0f;
		// Shadows are drawn up to this far from the camera, in meters
		float ShadowDistance = 60.0f;
		// The size of the light's source: larger makes shadows softer further from their casters
		float ShadowSoftness = 1.0f;
	};

	// Registers the components above
	void RegisterRenderComponents(ComponentRegistry& registry);

}
