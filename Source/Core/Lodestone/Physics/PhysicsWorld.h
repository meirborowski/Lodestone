#pragma once

#include "Lodestone/Asset/Model.h"
#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/UUID.h"
#include "Lodestone/Simulation/Simulation.h"

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Lodestone {

	class Scene;

	enum class PhysicsEventType : uint32_t
	{
		// Two solid bodies, or a character and something solid, started touching
		CollisionBegin = 0,
		// They stopped touching, or one of them went away
		CollisionEnd = 1,
		// A body or character started overlapping a trigger
		TriggerEnter = 2,
		// It stopped overlapping it, or one of them went away
		TriggerExit = 3,
	};

	std::string_view ToString(PhysicsEventType type);

	// Two entities starting or stopping to touch during a tick. For trigger events, the first entity is the trigger
	struct PhysicsEvent
	{
		PhysicsEventType Type = PhysicsEventType::CollisionBegin;
		UUID First;
		UUID Second;

		bool operator==(const PhysicsEvent& other) const = default;
	};

	struct RaycastHit
	{
		// The entity whose body was hit
		UUID Entity;
		// Where the ray hit, and the surface's normal there, in world space
		glm::vec3 Point{0.0f};
		glm::vec3 Normal{0.0f};
		// How far along the ray, in meters
		float Distance = 0.0f;
	};

	struct RaycastOptions
	{
		// In meters
		float MaxDistance = 1000.0f;
		// Whether triggers can be hit
		bool IncludeTriggers = false;
		// An entity whose body the ray passes through, such as the one casting it
		UUID Ignore;
	};

	struct PhysicsWorldConfig
	{
		// Finds the models mesh colliders use. Without it, or for a model it can't find, mesh colliders are left out
		ModelLookup Models;
		// The most bodies the world holds at once, characters included
		uint32_t MaxBodies = 65536;
		// The most pairs of bodies that can be close enough to touch, and touching, at once
		uint32_t MaxBodyPairs = 65536;
		uint32_t MaxContactConstraints = 16384;
		// Threads that help step the world beside the one calling Update(); by default, one fewer than the CPU has,
		// up to 4. Results don't depend on it
		std::optional<uint32_t> WorkerThreads;
		// Scratch memory for a step, in bytes, allocated up front; a step that needs more allocates it as it goes
		uint32_t TempMemory = 16u * 1024 * 1024;
	};

	// A scene's physics world, run by Jolt (see docs/Features/Physics.md and docs/Decisions/0021-physics.md). The
	// physics components describe it: each tick, Update() brings the world in line with them, steps it, and writes
	// simulated transforms and velocities back to the scene. It's a stateful simulation system, so saving and
	// restoring the simulation saves and restores the world with it, and re-simulating the same input commands gives
	// the same result.
	//
	// Used from one thread at a time
	class PhysicsWorld final : public StatefulSystem
	{
	public:
		// The scene must outlive the world
		explicit PhysicsWorld(Scene& scene, PhysicsWorldConfig config = {});
		~PhysicsWorld() override;

		PhysicsWorld(const PhysicsWorld&) = delete;
		PhysicsWorld& operator=(const PhysicsWorld&) = delete;
		PhysicsWorld(PhysicsWorld&&) = delete;
		PhysicsWorld& operator=(PhysicsWorld&&) = delete;

		// One tick: syncs the world with the scene (see Sync()), moves characters by their players' input commands,
		// steps the bodies by the tick's length, writes transforms and velocities back, and gathers the tick's events
		void Update(SimulationContext& context) override;
		void SaveState(std::vector<std::byte>& state) const override;
		void RestoreState(std::span<const std::byte> state) override;

		// Brings the world in line with the scene's components without stepping it, so it can be queried: creates,
		// updates and removes bodies, and moves static and kinematic bodies to their entities. Doesn't change the
		// scene. Update() does this first
		void Sync();

		// The collision and trigger events of the latest tick, in the same order every run
		std::span<const PhysicsEvent> GetEvents() const;

		// The closest hit along a ray from the origin in the direction (which needn't be normalized), if any. When
		// several bodies are hit at the same distance, the result is the same every run
		std::optional<RaycastHit> Raycast(
			const glm::vec3& origin, const glm::vec3& direction, const RaycastOptions& options = {}) const;
		// Every body hit along the ray, nearest first, one hit each
		std::vector<RaycastHit> RaycastAll(
			const glm::vec3& origin, const glm::vec3& direction, const RaycastOptions& options = {}) const;

		// The bodies in the world, characters' included
		uint32_t GetBodyCount() const;
		// Whether the entity has a body or a character in the world
		bool HasBody(UUID entity) const;
		// Whether the entity's body is dynamic or kinematic and asleep: at rest, and not simulated until something
		// disturbs it
		bool IsSleeping(UUID entity) const;

	private:
		class Implementation;

	private:
		Scope<Implementation> m_Implementation;
	};

}
