#include "Lodestone/Physics/PhysicsComponents.h"

#include "Common/DescribeError.h"
#include "Lodestone/Reflection/ComponentRegistry.h"
#include "Lodestone/Scene/Entity.h"
#include "Lodestone/Scene/SceneSerializer.h"

#include <doctest/doctest.h>

#include <string>
#include <utility>

namespace Lodestone {

	TEST_CASE("The physics components are registered with the engine, with defaults within their limits")
	{
		const ComponentRegistry& registry = GetEngineComponentRegistry();
		for (const char* name : {"RigidBody", "BoxCollider", "SphereCollider", "CapsuleCollider", "MeshCollider",
				 "CharacterController", "PhysicsSettings"})
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
		CHECK(registry.Find<RigidBodyComponent>() == registry.Find("RigidBody"));
		CHECK(registry.Find<MeshColliderComponent>()->FindField("Model")->GetType() == FieldType::UUID);

		// Clients predicting their character need the world's colliders and the character's state; only the server
		// simulates bodies' velocities
		const ComponentType& body = *registry.Find("RigidBody");
		for (const FieldInfo& field : body.GetFields())
		{
			CAPTURE(field.GetName());
			const bool velocity = field.GetName() == "LinearVelocity" || field.GetName() == "AngularVelocity";
			CHECK(field.IsReplicated() == !velocity);
			CHECK_FALSE(field.IsReadOnly());
		}
		const ComponentType& character = *registry.Find("CharacterController");
		for (const FieldInfo& field : character.GetFields())
		{
			CAPTURE(field.GetName());
			CHECK(field.IsReplicated());
			CHECK(field.IsReadOnly() == (field.GetName() == "Grounded"));
		}
		CHECK(registry.Find("BoxCollider")->FindField("Size")->IsReplicated());
	}

	TEST_CASE("Scenes save and load their physics components")
	{
		Scene scene;
		Entity crate = scene.CreateEntity("Crate");
		crate.Add<RigidBodyComponent>(RigidBodyComponent{
			.Type = static_cast<uint32_t>(BodyType::Kinematic),
			.Mass = 12.5f,
			.Friction = 0.8f,
			.Restitution = 0.25f,
			.LinearDamping = 0.5f,
			.AngularDamping = 0.75f,
			.GravityFactor = 0.5f,
			.IsTrigger = true,
			.ContinuousCollision = true,
			.AllowSleeping = false,
			.LinearVelocity = glm::vec3(1.0f, 2.0f, 3.0f),
			.AngularVelocity = glm::vec3(-90.0f, 0.0f, 45.0f),
		});
		crate.Add<BoxColliderComponent>(
			BoxColliderComponent{.Size = glm::vec3(1.0f, 2.0f, 3.0f), .Center = glm::vec3(0.5f)});
		crate.Add<SphereColliderComponent>(
			SphereColliderComponent{.Radius = 0.75f, .Center = glm::vec3(0.0f, 1.0f, 0.0f)});
		crate.Add<CapsuleColliderComponent>(CapsuleColliderComponent{.Height = 3.0f, .Radius = 0.4f});
		const UUID model = UUID::Generate();
		crate.Add<MeshColliderComponent>(MeshColliderComponent{.Model = model, .Mesh = 2, .Convex = true});
		Entity player = scene.CreateEntity("Player");
		player.Add<CharacterControllerComponent>(CharacterControllerComponent{
			.Player = 3,
			.Height = 1.6f,
			.Radius = 0.35f,
			.MoveSpeed = 6.0f,
			.JumpSpeed = 4.0f,
			.AirAcceleration = 15.0f,
			.MaxSlope = 50.0f,
			.StepHeight = 0.4f,
			.MouseTurnSpeed = -0.2f,
			.StickTurnSpeed = 90.0f,
			.Mass = 80.0f,
			.PushStrength = 250.0f,
			.Velocity = glm::vec3(0.0f, -1.0f, 2.0f),
			.Grounded = true,
		});
		scene.CreateEntity("Settings")
			.Add<PhysicsSettingsComponent>(PhysicsSettingsComponent{.Gravity = glm::vec3(0.0f, -3.7f, 0.0f)});

		const std::string text = SceneSerializer::SerializeToText(scene);
		const auto loaded = SceneSerializer::DeserializeFromText(text);

		REQUIRE_MESSAGE(loaded.has_value(), Testing::DescribeError(loaded));
		CHECK(SceneSerializer::SerializeToText(**loaded) == text);
		const Entity loadedCrate = (*loaded)->FindEntity(crate.GetId());
		const auto& body = loadedCrate.Get<RigidBodyComponent>();
		CHECK(body.Type == static_cast<uint32_t>(BodyType::Kinematic));
		CHECK(body.Mass == 12.5f);
		CHECK(body.IsTrigger);
		CHECK_FALSE(body.AllowSleeping);
		CHECK(body.AngularVelocity == glm::vec3(-90.0f, 0.0f, 45.0f));
		CHECK(loadedCrate.Get<BoxColliderComponent>().Size == glm::vec3(1.0f, 2.0f, 3.0f));
		CHECK(loadedCrate.Get<SphereColliderComponent>().Center == glm::vec3(0.0f, 1.0f, 0.0f));
		CHECK(loadedCrate.Get<CapsuleColliderComponent>().Height == 3.0f);
		CHECK(loadedCrate.Get<MeshColliderComponent>().Model == model);
		CHECK(loadedCrate.Get<MeshColliderComponent>().Convex);
		const auto& character = (*loaded)->FindEntity(player.GetId()).Get<CharacterControllerComponent>();
		CHECK(character.Player == 3);
		CHECK(character.MouseTurnSpeed == -0.2f);
		CHECK(character.AirAcceleration == 15.0f);
		CHECK(character.Velocity == glm::vec3(0.0f, -1.0f, 2.0f));
		CHECK(character.Grounded);
	}

	TEST_CASE("Physics fields outside their limits fail to load")
	{
		Scene scene;
		Entity entity = scene.CreateEntity("Crate");
		entity.Add<RigidBodyComponent>();
		entity.Add<CharacterControllerComponent>();
		const std::string text = SceneSerializer::SerializeToText(scene);
		for (const auto& [field, invalid] : {
				 std::pair(R"("Type": 2)", R"("Type": 3)"),
				 std::pair(R"("Restitution": 0)", R"("Restitution": 1.5)"),
				 std::pair(R"("Mass": 1)", R"("Mass": 0)"),
				 std::pair(R"("MaxSlope": 45)", R"("MaxSlope": 90)"),
			 })
		{
			CAPTURE(invalid);
			std::string changed = text;
			const size_t position = changed.find(field);
			REQUIRE(position != std::string::npos);
			changed.replace(position, std::string(field).size(), invalid);
			CHECK_FALSE(SceneSerializer::DeserializeFromText(changed).has_value());
		}
	}

}
