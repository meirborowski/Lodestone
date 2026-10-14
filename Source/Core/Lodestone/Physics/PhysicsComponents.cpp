#include "Lodestone/Physics/PhysicsComponents.h"

#include "Lodestone/Reflection/ComponentRegistry.h"

namespace Lodestone {

	void RegisterPhysicsComponents(ComponentRegistry& registry)
	{
		// Clients predicting their character need the world's colliders as the server has them, so the physics
		// components replicate - except bodies' velocities, which only the server simulates
		constexpr auto replicated = FieldFlags::Replicated;
		constexpr auto engineWritten = FieldFlags::Replicated | FieldFlags::ReadOnly;

		registry
			.Register<RigidBodyComponent>("RigidBody",
				"Makes the entity a body in the physics world, shaped by the colliders on it and its descendants")
			.Field("Type", &RigidBodyComponent::Type,
				{.Description = "0: static, 1: kinematic (follows its transform), 2: dynamic (simulated)",
					.Max = 2.0,
					.Flags = replicated})
			.Field("Mass", &RigidBodyComponent::Mass,
				{.Description = "In kilograms, for dynamic bodies", .Min = 0.001, .Max = 1.0e9, .Flags = replicated})
			.Field("Friction", &RigidBodyComponent::Friction,
				{.Description = "How much it resists sliding along surfaces: 0 is ice",
					.Min = 0.0,
					.Max = 10.0,
					.Flags = replicated})
			.Field("Restitution", &RigidBodyComponent::Restitution,
				{.Description = "How much speed it keeps after a hit: 0 doesn't bounce, 1 bounces back fully",
					.Min = 0.0,
					.Max = 1.0,
					.Flags = replicated})
			.Field("LinearDamping", &RigidBodyComponent::LinearDamping,
				{.Description = "How quickly its movement slows down by itself",
					.Min = 0.0,
					.Max = 100.0,
					.Flags = replicated})
			.Field("AngularDamping", &RigidBodyComponent::AngularDamping,
				{.Description = "How quickly its spinning slows down by itself",
					.Min = 0.0,
					.Max = 100.0,
					.Flags = replicated})
			.Field("GravityFactor", &RigidBodyComponent::GravityFactor,
				{.Description = "How much gravity pulls it: 0 floats, 1 falls normally",
					.Min = -100.0,
					.Max = 100.0,
					.Flags = replicated})
			.Field("IsTrigger", &RigidBodyComponent::IsTrigger,
				{.Description = "Detects bodies and characters overlapping it instead of colliding",
					.Flags = replicated})
			.Field("ContinuousCollision", &RigidBodyComponent::ContinuousCollision,
				{.Description = "Sweeps its motion so it can't pass through thin objects when fast. Costs more",
					.Flags = replicated})
			.Field("AllowSleeping", &RigidBodyComponent::AllowSleeping,
				{.Description = "Lets it stop simulating at rest, until something disturbs it", .Flags = replicated})
			.Field("LinearVelocity", &RigidBodyComponent::LinearVelocity,
				{.Description = "In meters per second, in world space", .Min = -1.0e4, .Max = 1.0e4})
			.Field("AngularVelocity", &RigidBodyComponent::AngularVelocity,
				{.Description = "In degrees per second, around each world axis", .Min = -1.0e5, .Max = 1.0e5});

		registry.Register<BoxColliderComponent>("BoxCollider", "A box collider, scaled with the entity")
			.Field("Size", &BoxColliderComponent::Size,
				{.Description = "Edge lengths along X, Y and Z, in meters",
					.Min = 0.001,
					.Max = 1.0e5,
					.Flags = replicated})
			.Field("Center", &BoxColliderComponent::Center,
				{.Description = "Relative to the entity", .Min = -1.0e5, .Max = 1.0e5, .Flags = replicated});

		registry.Register<SphereColliderComponent>("SphereCollider", "A sphere collider, scaled with the entity")
			.Field("Radius", &SphereColliderComponent::Radius,
				{.Description = "In meters", .Min = 0.001, .Max = 1.0e5, .Flags = replicated})
			.Field("Center", &SphereColliderComponent::Center,
				{.Description = "Relative to the entity", .Min = -1.0e5, .Max = 1.0e5, .Flags = replicated});

		registry
			.Register<CapsuleColliderComponent>(
				"CapsuleCollider", "A capsule collider along the entity's Y axis, scaled with the entity")
			.Field("Height", &CapsuleColliderComponent::Height,
				{.Description = "End to end, in meters; under twice the radius makes a sphere",
					.Min = 0.0,
					.Max = 1.0e5,
					.Flags = replicated})
			.Field("Radius", &CapsuleColliderComponent::Radius,
				{.Description = "In meters", .Min = 0.001, .Max = 1.0e5, .Flags = replicated})
			.Field("Center", &CapsuleColliderComponent::Center,
				{.Description = "Relative to the entity", .Min = -1.0e5, .Max = 1.0e5, .Flags = replicated});

		registry
			.Register<MeshColliderComponent>(
				"MeshCollider", "A collider from a model's mesh: its triangles, or their convex hull")
			.Field(
				"Model", &MeshColliderComponent::Model, {.Description = "The model asset's UUID", .Flags = replicated})
			.Field("Mesh", &MeshColliderComponent::Mesh,
				{.Description = "Which of the model's meshes, from 0", .Flags = replicated})
			.Field("Convex", &MeshColliderComponent::Convex,
				{.Description = "Uses the mesh's convex hull: faster, and the only kind dynamic bodies can have",
					.Flags = replicated});

		registry
			.Register<CharacterControllerComponent>("CharacterController",
				"Moves the entity as a character its player controls: WASD, arrows or left stick walk, mouse or right "
				"stick turn, Space or gamepad A jump")
			.Field("Player", &CharacterControllerComponent::Player,
				{.Description = "Whose input commands drive it", .Max = 255.0, .Flags = replicated})
			.Field("Height", &CharacterControllerComponent::Height,
				{.Description = "From its feet to the top of its head, in meters",
					.Min = 0.1,
					.Max = 100.0,
					.Flags = replicated})
			.Field("Radius", &CharacterControllerComponent::Radius,
				{.Description = "In meters", .Min = 0.01, .Max = 10.0, .Flags = replicated})
			.Field("MoveSpeed", &CharacterControllerComponent::MoveSpeed,
				{.Description = "In meters per second", .Min = 0.0, .Max = 1000.0, .Flags = replicated})
			.Field("JumpSpeed", &CharacterControllerComponent::JumpSpeed,
				{.Description = "Upward speed of a jump, in meters per second: 0 doesn't jump",
					.Min = 0.0,
					.Max = 1000.0,
					.Flags = replicated})
			.Field("AirAcceleration", &CharacterControllerComponent::AirAcceleration,
				{.Description = "How quickly the player steers it in the air, in meters per second squared: 0 keeps "
								"its momentum",
					.Min = 0.0,
					.Max = 1000.0,
					.Flags = replicated})
			.Field("MaxSlope", &CharacterControllerComponent::MaxSlope,
				{.Description = "The steepest slope it walks up, in degrees",
					.Min = 0.0,
					.Max = 89.0,
					.Flags = replicated})
			.Field("StepHeight", &CharacterControllerComponent::StepHeight,
				{.Description = "The tallest step it walks up, in meters",
					.Min = 0.0,
					.Max = 10.0,
					.Flags = replicated})
			.Field("MouseTurnSpeed", &CharacterControllerComponent::MouseTurnSpeed,
				{.Description = "Degrees turned per pixel of mouse movement: 0 ignores the mouse",
					.Min = -10.0,
					.Max = 10.0,
					.Flags = replicated})
			.Field("StickTurnSpeed", &CharacterControllerComponent::StickTurnSpeed,
				{.Description = "Degrees turned per second with the right stick all the way over",
					.Min = -3600.0,
					.Max = 3600.0,
					.Flags = replicated})
			.Field("Mass", &CharacterControllerComponent::Mass,
				{.Description = "In kilograms, for pushing dynamic bodies",
					.Min = 0.001,
					.Max = 1.0e6,
					.Flags = replicated})
			.Field("PushStrength", &CharacterControllerComponent::PushStrength,
				{.Description = "The most force it pushes dynamic bodies with, in newtons",
					.Min = 0.0,
					.Max = 1.0e9,
					.Flags = replicated})
			.Field("Velocity", &CharacterControllerComponent::Velocity,
				{.Description = "In meters per second, in world space",
					.Min = -1.0e4,
					.Max = 1.0e4,
					.Flags = replicated})
			.Field("Grounded", &CharacterControllerComponent::Grounded,
				{.Description = "Whether it stands on ground it can walk on", .Flags = engineWritten});

		registry.Register<PhysicsSettingsComponent>("PhysicsSettings", "The physics world's settings. One per scene")
			.Field("Gravity", &PhysicsSettingsComponent::Gravity,
				{.Description = "In meters per second squared", .Min = -1000.0, .Max = 1000.0, .Flags = replicated});
	}

}
