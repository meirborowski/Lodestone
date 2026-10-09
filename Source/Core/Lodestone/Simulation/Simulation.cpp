#include "Lodestone/Simulation/Simulation.h"

#include "Lodestone/Core/Assert.h"
#include "Lodestone/Core/Log.h"
#include "Lodestone/Scene/Entity.h"

#include <glm/common.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace Lodestone {

	const InputCommand& SimulationContext::GetInput(uint32_t player) const
	{
		static const InputCommand Empty;
		const auto command = std::ranges::find(m_Inputs, player, &InputCommand::Player);
		return command != m_Inputs.end() ? *command : Empty;
	}

	Simulation::Simulation(Scene& scene, const SimulationConfig& config)
		: m_Scene(&scene), m_Config(config), m_TickDuration(1.0 / config.TickRate)
	{
		LS_CORE_ASSERT(config.TickRate > 0 && config.MaxTicksPerFrame > 0, "Invalid simulation configuration");
	}

	void Simulation::AddSystem(std::string name, SystemFunction system)
	{
		m_Systems.push_back({.Name = std::move(name), .Function = std::move(system)});
	}

	void Simulation::Step(std::span<const InputCommand> inputs)
	{
		for (size_t index = 0; index < inputs.size(); ++index)
		{
			LS_CORE_ASSERT(inputs[index].Tick == m_Tick, "An input command for tick {} was given to tick {}",
				inputs[index].Tick, m_Tick);
			LS_CORE_ASSERT(std::ranges::count(inputs, inputs[index].Player, &InputCommand::Player) == 1,
				"Player {} has more than one input command for tick {}", inputs[index].Player, m_Tick);
		}

		// Where every entity starts this tick, for interpolation
		entt::registry& registry = m_Scene->GetRegistry();
		for (const auto [entity, transform] : registry.view<const TransformComponent>().each())
			registry.emplace_or_replace<PreviousTransformComponent>(entity, transform);

		SimulationContext context(*m_Scene, m_Tick, static_cast<float>(m_TickDuration), inputs);
		for (const System& system : m_Systems)
			system.Function(context);
		++m_Tick;
	}

	uint32_t Simulation::Advance(double elapsedSeconds, const InputSampler& sampleInputs)
	{
		// A clock that goes backwards, or a broken measurement, adds no time
		if (std::isfinite(elapsedSeconds) && elapsedSeconds > 0.0)
			m_Accumulator += elapsedSeconds;

		uint32_t ticks = 0;
		while (m_Accumulator >= m_TickDuration && ticks < m_Config.MaxTicksPerFrame)
		{
			const std::vector<InputCommand> inputs = sampleInputs(m_Tick);
			Step(inputs);
			m_Accumulator -= m_TickDuration;
			++ticks;
		}

		if (m_Accumulator >= m_TickDuration)
		{
			LS_CORE_WARN("The simulation fell {:.0f} ms behind real time, which it skips",
				(m_Accumulator - std::fmod(m_Accumulator, m_TickDuration)) * 1000.0);
			m_Accumulator = std::fmod(m_Accumulator, m_TickDuration);
		}
		return ticks;
	}

	float Simulation::GetInterpolationFactor() const
	{
		return static_cast<float>(std::clamp(m_Accumulator / m_TickDuration, 0.0, 1.0));
	}

	TransformComponent Simulation::GetInterpolatedTransform(Entity entity) const
	{
		const TransformComponent& current = entity.GetTransform();
		const auto* previous = entity.TryGet<PreviousTransformComponent>();
		// Entities created during the latest tick have nowhere to come from
		if (previous == nullptr)
			return current;

		const float factor = GetInterpolationFactor();
		TransformComponent interpolated;
		interpolated.Position = glm::mix(previous->Transform.Position, current.Position, factor);
		interpolated.Rotation = glm::slerp(previous->Transform.Rotation, current.Rotation, factor);
		interpolated.Scale = glm::mix(previous->Transform.Scale, current.Scale, factor);
		return interpolated;
	}

	SimulationState Simulation::SaveState() const
	{
		return {.Tick = m_Tick, .Scene = m_Scene->SaveSnapshot()};
	}

	void Simulation::RestoreState(const SimulationState& state)
	{
		m_Scene->RestoreSnapshot(state.Scene);
		m_Tick = state.Tick;
	}

}
