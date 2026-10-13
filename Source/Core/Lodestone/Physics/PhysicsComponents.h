#pragma once

#include "Lodestone/Core/UUID.h"

#include <glm/vec3.hpp>

#include <cstdint>

// The components that put entities in the physics world. They're data: PhysicsWorld creates and updates Jolt bodies
// from them each tick, and writes simulated transforms and velocities back (see docs/Decisions/0021-physics.md). Each
// is registered in RegisterPhysicsComponents()

namespace Lodestone {

	class ComponentRegistry;

	enum class BodyType : uint32_t
	{
		// Doesn't move, unless its entity is moved, which teleports it
		Static = 0,
		// Follows its entity's transform, pushing dynamic bodies out of the way; forces don't affect it
		Kinematic = 1,
		// Moved by the simulation: gravity, collisions and its velocity
		Dynamic = 2,
	};

	// Makes the entity a body in the physics world, shaped by the colliders on it and on its descendants that have no
	// body of their own
	struct RigidBodyComponent
	{
		// A BodyType
		uint32_t Type = static_cast<uint32_t>(BodyType::Dynamic);
		// In kilograms, for dynamic bodies
		float Mass = 1.0f;
		// How much it resists sliding along surfaces: 0 is ice
		float Friction = 0.5f;
		// How much speed it keeps after hitting something: 0 doesn't bounce, 1 bounces back fully
		float Restitution = 0.0f;
		// How quickly it slows down by itself
		float LinearDamping = 0.05f;
		float AngularDamping = 0.05f;
		// How much gravity pulls it: 0 floats, 1 falls normally
		float GravityFactor = 1.0f;
		// Detects bodies and characters overlapping it, instead of colliding with them
		bool IsTrigger = false;
		// Sweeps its motion each tick, so it can't pass through thin objects when it's fast. Costs more
		bool ContinuousCollision = false;
		// Lets it stop simulating when it comes to rest, until something disturbs it
		bool AllowSleeping = true;
		// In meters per second, in world space. Physics writes it each tick; setting it changes the velocity
		glm::vec3 LinearVelocity{0.0f};
		// In degrees per second, around each world axis
		glm::vec3 AngularVelocity{0.0f};
	};

	// A box on the entity, scaled with it
	struct BoxColliderComponent
	{
		// Its edge lengths along the entity's X, Y and Z axes, in meters
		glm::vec3 Size{1.0f};
		// Where its center is, relative to the entity
		glm::vec3 Center{0.0f};
	};

	// A sphere on the entity, scaled with it
	struct SphereColliderComponent
	{
		float Radius = 0.5f;
		glm::vec3 Center{0.0f};
	};

	// A capsule on the entity along its Y axis, scaled with it: a cylinder with a hemisphere on each end
	struct CapsuleColliderComponent
	{
		// From end to end, hemispheres included. A height under twice the radius makes a sphere
		float Height = 2.0f;
		float Radius = 0.5f;
		glm::vec3 Center{0.0f};
	};

	// The triangles of a mesh of a model asset, or their convex hull, scaled with the entity
	struct MeshColliderComponent
	{
		// The model asset
		UUID Model;
		// Which of the model's meshes
		uint32_t Mesh = 0;
		// Uses the mesh's convex hull: faster to collide with, and the only kind dynamic bodies can have
		bool Convex = false;
	};

	// Moves the entity as a character its player controls: a capsule standing on the entity's position, kept upright,
	// driven by the player's input commands. WASD, the arrow keys and the left stick walk relative to where it faces;
	// the mouse and the right stick turn it; Space and gamepad A jump
	struct CharacterControllerComponent
	{
		// Whose input commands drive it
		uint32_t Player = 0;
		// From its feet, the entity's position, to the top of its head, in meters
		float Height = 1.8f;
		float Radius = 0.3f;
		// In meters per second
		float MoveSpeed = 4.0f;
		// Upward speed when it jumps, in meters per second: 0 doesn't jump
		float JumpSpeed = 5.0f;
		// How quickly the player steers it in the air, toward walking speed, in meters per second squared: 0 keeps
		// its momentum until it lands
		float AirAcceleration = 10.0f;
		// The steepest slope it can walk up, in degrees
		float MaxSlope = 45.0f;
		// The tallest step it walks up, in meters
		float StepHeight = 0.3f;
		// Degrees it turns per pixel the mouse moves: 0 ignores the mouse
		float MouseTurnSpeed = 0.1f;
		// Degrees it turns per second with the right stick all the way to one side
		float StickTurnSpeed = 180.0f;
		// In kilograms, for pushing dynamic bodies
		float Mass = 70.0f;
		// The most force it pushes dynamic bodies with, in newtons
		float PushStrength = 100.0f;
		// In meters per second, in world space. Physics writes it each tick; setting it changes the velocity
		glm::vec3 Velocity{0.0f};
		// Whether it's standing on ground it can walk on. Physics writes it each tick
		bool Grounded = false;
	};

	// The physics world's settings. One per scene; without one, the defaults apply
	struct PhysicsSettingsComponent
	{
		// In meters per second squared
		glm::vec3 Gravity{0.0f, -9.81f, 0.0f};
	};

	// Registers the components above
	void RegisterPhysicsComponents(ComponentRegistry& registry);

}
