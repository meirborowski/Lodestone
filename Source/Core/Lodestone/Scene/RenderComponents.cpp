#include "Lodestone/Scene/RenderComponents.h"

#include "Lodestone/Reflection/ComponentRegistry.h"

namespace Lodestone {

	void RegisterRenderComponents(ComponentRegistry& registry)
	{
		registry.Register<MeshRendererComponent>("MeshRenderer", "Draws a mesh of a model asset")
			.Field("Model", &MeshRendererComponent::Model, {.Description = "The model asset's UUID"})
			.Field("Mesh", &MeshRendererComponent::Mesh, {.Description = "Which of the model's meshes, from 0"})
			.Field(
				"CastShadows", &MeshRendererComponent::CastShadows, {.Description = "Whether the mesh casts shadows"});

		registry.Register<CameraComponent>("Camera", "A camera the game renders from, looking along -Z")
			.Field("FieldOfView", &CameraComponent::FieldOfView,
				{.Description = "Vertical, in degrees", .Min = 1.0, .Max = 179.0})
			.Field("NearPlane", &CameraComponent::NearPlane,
				{.Description = "Nothing closer is drawn, in meters", .Min = 0.001, .Max = 1000.0})
			.Field("FarPlane", &CameraComponent::FarPlane,
				{.Description = "Nothing farther is drawn, in meters", .Min = 0.01, .Max = 1000000.0})
			.Field("Primary", &CameraComponent::Primary,
				{.Description = "The camera the game renders from, when the scene has several"});

		registry
			.Register<DirectionalLightComponent>(
				"DirectionalLight", "Light from far away, like the sun, shining along the entity's -Z")
			.Field("Color", &DirectionalLightComponent::Color,
				{.Description = "Linear RGB", .Min = 0.0, .Max = 1.0, .Flags = FieldFlags::Replicated})
			.Field("Intensity", &DirectionalLightComponent::Intensity,
				{.Description = "Brightness", .Min = 0.0, .Max = 1000.0, .Flags = FieldFlags::Replicated})
			.Field("CastShadows", &DirectionalLightComponent::CastShadows, {.Description = "Whether it casts shadows"});

		registry.Register<PointLightComponent>("PointLight", "Light from a point in every direction")
			.Field("Color", &PointLightComponent::Color,
				{.Description = "Linear RGB", .Min = 0.0, .Max = 1.0, .Flags = FieldFlags::Replicated})
			.Field("Intensity", &PointLightComponent::Intensity,
				{.Description = "Brightness", .Min = 0.0, .Max = 100000.0, .Flags = FieldFlags::Replicated})
			.Field("Range", &PointLightComponent::Range,
				{.Description = "The light fades to nothing at this distance, in meters",
					.Min = 0.01,
					.Max = 10000.0,
					.Flags = FieldFlags::Replicated});

		registry.Register<SpotLightComponent>("SpotLight", "Light from a point, in a cone along the entity's -Z")
			.Field("Color", &SpotLightComponent::Color,
				{.Description = "Linear RGB", .Min = 0.0, .Max = 1.0, .Flags = FieldFlags::Replicated})
			.Field("Intensity", &SpotLightComponent::Intensity,
				{.Description = "Brightness", .Min = 0.0, .Max = 100000.0, .Flags = FieldFlags::Replicated})
			.Field("Range", &SpotLightComponent::Range,
				{.Description = "The light fades to nothing at this distance, in meters",
					.Min = 0.01,
					.Max = 10000.0,
					.Flags = FieldFlags::Replicated})
			.Field("InnerAngle", &SpotLightComponent::InnerAngle,
				{.Description = "Full brightness within this angle of the axis, in degrees",
					.Min = 0.0,
					.Max = 89.0,
					.Flags = FieldFlags::Replicated})
			.Field("OuterAngle", &SpotLightComponent::OuterAngle,
				{.Description = "No light beyond this angle from the axis, in degrees",
					.Min = 0.0,
					.Max = 89.0,
					.Flags = FieldFlags::Replicated});

		registry
			.Register<EnvironmentComponent>(
				"Environment", "Image-based lighting and the sky, from an HDR environment map. One per scene")
			.Field("Environment", &EnvironmentComponent::Environment,
				{.Description = "The environment asset's UUID (an .hdr file); nil for a plain grey sky"})
			.Field("Intensity", &EnvironmentComponent::Intensity,
				{.Description = "Brightness of the lighting and the sky", .Min = 0.0, .Max = 100.0})
			.Field("Rotation", &EnvironmentComponent::Rotation,
				{.Description = "Turns the environment around the vertical axis, in degrees",
					.Min = -360.0,
					.Max = 360.0})
			.Field("ShowSky", &EnvironmentComponent::ShowSky,
				{.Description = "Whether the environment is drawn behind everything"});

		registry
			.Register<RenderSettingsComponent>(
				"RenderSettings", "Exposure, tonemapping, ambient occlusion and shadows. One per scene")
			.Field("Exposure", &RenderSettingsComponent::Exposure,
				{.Description = "In stops: +1 doubles the brightness", .Min = -16.0, .Max = 16.0})
			.Field("Tonemapper", &RenderSettingsComponent::Tonemapper,
				{.Description = "0: ACES filmic, 1: Khronos PBR Neutral, 2: none (clamped)", .Max = 2.0})
			.Field("AmbientOcclusion", &RenderSettingsComponent::AmbientOcclusion,
				{.Description = "Darkens creases and corners (SSAO)"})
			.Field("AmbientOcclusionRadius", &RenderSettingsComponent::AmbientOcclusionRadius,
				{.Description = "How far occlusion reaches, in meters", .Min = 0.01, .Max = 10.0})
			.Field("AmbientOcclusionIntensity", &RenderSettingsComponent::AmbientOcclusionIntensity,
				{.Description = "How dark occlusion gets", .Min = 0.0, .Max = 4.0})
			.Field("ShadowDistance", &RenderSettingsComponent::ShadowDistance,
				{.Description = "Shadows are drawn up to this far from the camera, in meters",
					.Min = 1.0,
					.Max = 10000.0})
			.Field("ShadowSoftness", &RenderSettingsComponent::ShadowSoftness,
				{.Description = "The light's size: larger makes shadows softer away from their casters",
					.Min = 0.0,
					.Max = 10.0});
	}

}
