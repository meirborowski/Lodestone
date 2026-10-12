#include "Lodestone/Scene/RenderComponents.h"

#include "Common/DescribeError.h"
#include "Lodestone/Reflection/ComponentRegistry.h"
#include "Lodestone/Scene/Entity.h"
#include "Lodestone/Scene/SceneSerializer.h"

#include <doctest/doctest.h>

#include <string>

namespace Lodestone {

	TEST_CASE("The rendering components are registered with the engine, with defaults within their limits")
	{
		const ComponentRegistry& registry = GetEngineComponentRegistry();
		for (const char* name :
			{"MeshRenderer", "Camera", "DirectionalLight", "PointLight", "SpotLight", "Environment", "RenderSettings"})
		{
			CAPTURE(name);
			const ComponentType* type = registry.Find(name);
			REQUIRE(type != nullptr);
			CHECK_FALSE(type->IsRequired());
			CHECK_FALSE(type->IsInternal());
			CHECK_FALSE(type->GetDescription().empty());
			for (const FieldInfo& field : type->GetFields())
			{
				CAPTURE(field.GetName());
				CHECK_FALSE(field.GetDescription().empty());
				CHECK(field.Validate(field.GetDefault()).has_value());
			}
		}
		CHECK(registry.Find<MeshRendererComponent>() == registry.Find("MeshRenderer"));
		CHECK(registry.Find("RenderSettings")->FindField("Tonemapper")->GetMax() == 2.0);
	}

	TEST_CASE("Scenes save and load their rendering components")
	{
		Scene scene;
		Entity model = scene.CreateEntity("Model");
		const UUID modelAsset = UUID::Generate();
		model.Add<MeshRendererComponent>(MeshRendererComponent{.Model = modelAsset, .Mesh = 2, .CastShadows = false});
		Entity sun = scene.CreateEntity("Sun");
		sun.Add<DirectionalLightComponent>(
			DirectionalLightComponent{.Color = glm::vec3(1.0f, 0.9f, 0.8f), .Intensity = 4.0f, .CastShadows = true});
		Entity lamp = scene.CreateEntity("Lamp");
		lamp.Add<PointLightComponent>(PointLightComponent{.Color = glm::vec3(1.0f, 0.5f, 0.2f), .Intensity = 20.0f});
		lamp.Add<SpotLightComponent>(SpotLightComponent{.InnerAngle = 10.0f, .OuterAngle = 20.0f});
		Entity settings = scene.CreateEntity("Settings");
		settings.Add<EnvironmentComponent>(EnvironmentComponent{.Environment = UUID::Generate(), .Rotation = 90.0f});
		settings.Add<RenderSettingsComponent>(
			RenderSettingsComponent{.Exposure = 1.5f, .Tonemapper = 1, .AmbientOcclusion = false});
		settings.Add<CameraComponent>(CameraComponent{.FieldOfView = 45.0f, .Primary = false});

		const std::string text = SceneSerializer::SerializeToText(scene);
		const auto loaded = SceneSerializer::DeserializeFromText(text);

		REQUIRE_MESSAGE(loaded.has_value(), Testing::DescribeError(loaded));
		CHECK(SceneSerializer::SerializeToText(**loaded) == text);
		const Entity loadedModel = (*loaded)->FindEntity(model.GetId());
		CHECK(loadedModel.Get<MeshRendererComponent>().Model == modelAsset);
		CHECK(loadedModel.Get<MeshRendererComponent>().Mesh == 2);
		CHECK_FALSE(loadedModel.Get<MeshRendererComponent>().CastShadows);
		const Entity loadedSettings = (*loaded)->FindEntity(settings.GetId());
		CHECK(loadedSettings.Get<RenderSettingsComponent>().Tonemapper == 1);
		CHECK(loadedSettings.Get<EnvironmentComponent>().Rotation == 90.0f);
		CHECK(loadedSettings.Get<CameraComponent>().FieldOfView == 45.0f);
	}

	TEST_CASE("Rendering component fields outside their limits fail to load")
	{
		Scene scene;
		Entity light = scene.CreateEntity("Light");
		light.Add<SpotLightComponent>();
		std::string text = SceneSerializer::SerializeToText(scene);
		const std::string field = R"("OuterAngle": 35)";
		const size_t position = text.find(field);
		REQUIRE(position != std::string::npos);
		text.replace(position, field.size(), R"("OuterAngle": 120)");

		CHECK_FALSE(SceneSerializer::DeserializeFromText(text).has_value());
	}

}
