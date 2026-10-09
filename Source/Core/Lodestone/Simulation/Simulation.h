#pragma once

#include "Lodestone/Input/InputCommand.h"
#include "Lodestone/Scene/Components.h"
#include "Lodestone/Scene/Scene.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace Lodestone {

	class Entity;

	struct SimulationConfig
	{
		// Ticks per second
		uint32_t TickRate = 60;
		// The most ticks Advance() runs for one frame. When frames take longer than that, the simulation falls behind
		// real time instead of running ever more ticks per frame and never catching up
		uint32_t MaxTicksPerFrame = 8;
	};

	// What a system sees during one tick
	class SimulationContext
	{
	public:
		SimulationContext(Scene& scene, uint64_t tick, float deltaTime, std::span<const InputCommand> inputs)
			: m_Scene(&scene), m_Tick(tick), m_DeltaTime(deltaTime), m_Inputs(inputs)
		{
		}

		Scene& GetScene() const { return *m_Scene; }
		uint64_t GetTick() const { return m_Tick; }
		// The tick's length in seconds, always the same
		float GetDeltaTime() const { return m_DeltaTime; }

		// A player's input command for this tick. A player without one - not connected, or no input arrived - gets an
		// empty command
		const InputCommand& GetInput(uint32_t player) const;
		std::span<const InputCommand> GetInputs() const { return m_Inputs; }

	private:
		Scene* m_Scene;
		uint64_t m_Tick;
		float m_DeltaTime;
		std::span<const InputCommand> m_Inputs;
	};

	using SystemFunction = std::function<void(SimulationContext& context)>;

	// Everything needed to resume a simulation from a tick (see Simulation::SaveState)
	struct SimulationState
	{
		uint64_t Tick = 0;
		SceneSnapshot Scene;
	};

	// Runs a scene's game logic on a fixed tick, independent of the frame rate (see
	// docs/Architecture.md#simulation). Each tick, the systems run in order with that tick's input commands - the only
	// input the simulation sees. Given the same state and the same input commands, ticks produce the same result,
	// which is what client-side prediction relies on to roll back and re-simulate.
	//
	// Rendering runs between ticks, so it interpolates each entity's transform from the start of the latest tick to
	// its end (GetInterpolatedTransform)
	class Simulation
	{
	public:
		using InputSampler = std::function<std::vector<InputCommand>(uint64_t tick)>;

		explicit Simulation(Scene& scene, const SimulationConfig& config = {});

		// Systems run in the order they're added
		void AddSystem(std::string name, SystemFunction system);

		// Runs one tick with its input commands, at most one per player
		void Step(std::span<const InputCommand> inputs);
		// Adds real time, and runs the ticks that are due, asking for each one's input commands. Returns the number of
		// ticks run
		uint32_t Advance(double elapsedSeconds, const InputSampler& sampleInputs);

		// The number of ticks run so far, which is also the number of the next tick
		uint64_t GetTick() const { return m_Tick; }
		double GetTickDuration() const { return m_TickDuration; }
		// Where real time is between the latest tick and the next, from 0 to 1
		float GetInterpolationFactor() const;
		// The transform to render an entity with, between its transforms at the start and end of the latest tick
		TransformComponent GetInterpolatedTransform(Entity entity) const;

		// The state after the latest tick, which RestoreState() resumes from. Real time that hasn't made up a whole
		// tick yet isn't part of it
		[[nodiscard]] SimulationState SaveState() const;
		void RestoreState(const SimulationState& state);

		Scene& GetScene() const { return *m_Scene; }
		const SimulationConfig& GetConfig() const { return m_Config; }

	private:
		struct System
		{
			std::string Name;
			SystemFunction Function;
		};

	private:
		Scene* m_Scene;
		SimulationConfig m_Config;
		double m_TickDuration;
		std::vector<System> m_Systems;
		uint64_t m_Tick = 0;
		// Real time not yet simulated, in seconds
		double m_Accumulator = 0.0;
	};

}
