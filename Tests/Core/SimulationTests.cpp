#include "Lodestone/Simulation/Simulation.h"

#include "Lodestone/Scene/Entity.h"
#include "Lodestone/Scene/SceneSerializer.h"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace Lodestone {

	namespace {

		// Moves with the player's WASD keys
		struct MoverComponent
		{
			uint32_t Player = 0;
			float Speed = 1.0f;
		};

		// Flies forward until its lifetime runs out
		struct ProjectileComponent
		{
			float Lifetime = 1.0f;
		};

		const ComponentRegistry& GetTestComponents()
		{
			static const ComponentRegistry* s_Registry = []
			{
				auto* registry = new ComponentRegistry();
				RegisterCoreComponents(*registry);
				registry->Register<MoverComponent>("Mover")
					.Field("Player", &MoverComponent::Player)
					.Field("Speed", &MoverComponent::Speed);
				registry->Register<ProjectileComponent>("Projectile").Field("Lifetime", &ProjectileComponent::Lifetime);
				return registry;
			}();
			return *s_Registry;
		}

		void MoveSystem(SimulationContext& context)
		{
			Scene& scene = context.GetScene();
			for (const auto [entity, mover, transform] :
				scene.GetRegistry().view<const MoverComponent, TransformComponent>().each())
			{
				const InputCommand& input = context.GetInput(mover.Player);
				glm::vec3 direction(0.0f);
				direction.x += input.IsKeyDown(Key::D) ? 1.0f : 0.0f;
				direction.x -= input.IsKeyDown(Key::A) ? 1.0f : 0.0f;
				direction.z -= input.IsKeyDown(Key::W) ? 1.0f : 0.0f;
				direction.z += input.IsKeyDown(Key::S) ? 1.0f : 0.0f;
				transform.Position += direction * mover.Speed * context.GetDeltaTime();
			}
		}

		// Space fires a projectile from each mover; projectiles fly and expire
		void ProjectileSystem(SimulationContext& context)
		{
			Scene& scene = context.GetScene();
			entt::registry& registry = scene.GetRegistry();

			std::vector<entt::entity> expired;
			for (const auto [entity, projectile, transform] :
				registry.view<ProjectileComponent, TransformComponent>().each())
			{
				transform.Position.y += 5.0f * context.GetDeltaTime();
				projectile.Lifetime -= context.GetDeltaTime();
				if (projectile.Lifetime <= 0.0f)
					expired.push_back(entity);
			}
			for (const entt::entity entity : expired)
				scene.DestroyEntity(Entity(entity, &scene));

			std::vector<glm::vec3> origins;
			for (const auto [entity, mover, transform] :
				registry.view<const MoverComponent, const TransformComponent>().each())
			{
				if (context.GetInput(mover.Player).WasKeyPressed(Key::Space))
					origins.push_back(transform.Position);
			}
			for (const glm::vec3& origin : origins)
			{
				Entity projectile = scene.CreateEntity("Projectile");
				projectile.GetTransform().Position = origin;
				projectile.Add<ProjectileComponent>(ProjectileComponent{.Lifetime = 0.5f});
			}
		}

		// Deterministic, varied input for two players
		std::vector<InputCommand> MakeInputs(uint64_t tick)
		{
			std::vector<InputCommand> inputs(2);
			for (uint32_t player = 0; player < 2; ++player)
			{
				InputCommand& input = inputs[player];
				input.Tick = tick;
				input.Player = player;
				const uint64_t phase = (tick / 7 + uint64_t{player} * 3) % 8;
				input.KeysDown[std::to_underlying(Key::W)] = phase < 3;
				input.KeysDown[std::to_underlying(Key::A)] = phase % 3 == 1;
				input.KeysDown[std::to_underlying(Key::D)] = phase == 5;
				input.KeysPressed[std::to_underlying(Key::Space)] = (tick + uint64_t{player} * 5) % 11 == 0;
			}
			return inputs;
		}

		struct TestWorld
		{
			Scene WorldScene{GetTestComponents()};
			Simulation WorldSimulation{WorldScene};

			TestWorld()
			{
				for (uint32_t player = 0; player < 2; ++player)
				{
					Entity entity = WorldScene.CreateEntity(player == 0 ? "First" : "Second");
					entity.Add<MoverComponent>(
						MoverComponent{.Player = player, .Speed = 2.0f + static_cast<float>(player)});
				}
				WorldSimulation.AddSystem("Move", &MoveSystem);
				WorldSimulation.AddSystem("Projectiles", &ProjectileSystem);
			}

			void Run(uint64_t ticks)
			{
				for (uint64_t i = 0; i < ticks; ++i)
					WorldSimulation.Step(MakeInputs(WorldSimulation.GetTick()));
			}
		};

	}

	TEST_CASE("The simulation runs headless for many ticks from injected input commands")
	{
		Scene scene(GetTestComponents());
		Entity mover = scene.CreateEntity("Mover");
		mover.Add<MoverComponent>(MoverComponent{.Player = 3, .Speed = 4.0f});
		Simulation simulation(scene, {.TickRate = 50});
		simulation.AddSystem("Move", &MoveSystem);

		constexpr uint64_t tickCount = 10000;
		for (uint64_t tick = 0; tick < tickCount; ++tick)
		{
			// Player 3 holds D on even ticks and A every third tick; player 0's input is ignored
			InputCommand command;
			command.Tick = tick;
			command.Player = 3;
			command.KeysDown[std::to_underlying(Key::D)] = tick % 2 == 0;
			command.KeysDown[std::to_underlying(Key::A)] = tick % 3 == 0;
			InputCommand other;
			other.Tick = tick;
			other.KeysDown[std::to_underlying(Key::W)] = true;
			const std::array inputs = {other, command};
			simulation.Step(inputs);
		}

		CHECK(simulation.GetTick() == tickCount);
		// Net steps right: 5000 ticks with D, 3334 with A; each step is 4 m/s for 1/50 s
		const double expected = (5000.0 - 3334.0) * 4.0 / 50.0;
		CHECK(mover.GetTransform().Position.x == doctest::Approx(expected).epsilon(1e-4));
		CHECK(mover.GetTransform().Position.z == 0.0f);
	}

	TEST_CASE("Restoring a saved state and re-simulating the same input commands reproduces the same state")
	{
		TestWorld world;
		world.Run(100);
		const SimulationState saved = world.WorldSimulation.SaveState();
		CHECK(saved.Tick == 100);

		world.Run(400);
		const std::string original = SceneSerializer::SerializeToText(world.WorldScene);
		const size_t entityCount = world.WorldScene.GetEntityCount();
		// The run exercised spawning and destroying, not just movement
		CHECK(entityCount > 2);

		world.WorldSimulation.RestoreState(saved);
		CHECK(world.WorldSimulation.GetTick() == 100);
		world.Run(400);

		CHECK(world.WorldSimulation.GetTick() == 500);
		CHECK(world.WorldScene.GetEntityCount() == entityCount);
		CHECK(SceneSerializer::SerializeToText(world.WorldScene) == original);
	}

	TEST_CASE("Two simulations given the same input commands stay identical")
	{
		TestWorld first;
		TestWorld second;
		first.Run(300);
		second.Run(300);

		// Entity UUIDs come from each scene's own generator, so only the simulated values are compared
		const auto positions = [](Scene& scene)
		{
			std::vector<glm::vec3> result;
			for (const auto [entity, mover, transform] :
				scene.GetRegistry().view<const MoverComponent, const TransformComponent>().each())
				result.push_back(transform.Position);
			return result;
		};
		CHECK(positions(first.WorldScene) == positions(second.WorldScene));
		CHECK(first.WorldScene.GetEntityCount() == second.WorldScene.GetEntityCount());
	}

	TEST_CASE("Systems run in order, once per tick, with the tick's number and length")
	{
		Scene scene;
		Simulation simulation(scene, {.TickRate = 20});
		std::vector<std::string> calls;
		simulation.AddSystem("First",
			[&calls](SimulationContext& context)
			{
				calls.push_back("First " + std::to_string(context.GetTick()));
				CHECK(context.GetDeltaTime() == doctest::Approx(0.05f));
			});
		simulation.AddSystem("Second",
			[&calls](SimulationContext& context) { calls.push_back("Second " + std::to_string(context.GetTick())); });

		simulation.Step({});
		simulation.Step({});

		CHECK(calls == std::vector<std::string>{"First 0", "Second 0", "First 1", "Second 1"});
		CHECK(simulation.GetTick() == 2);
		CHECK(simulation.GetTickDuration() == doctest::Approx(0.05));
	}

	TEST_CASE("Players without an input command get an empty one")
	{
		Scene scene;
		Simulation simulation(scene);
		bool checked = false;
		simulation.AddSystem("Check",
			[&checked](SimulationContext& context)
			{
				CHECK(context.GetInput(1).IsKeyDown(Key::Space));
				CHECK_FALSE(context.GetInput(2).IsKeyDown(Key::Space));
				CHECK(context.GetInputs().size() == 1);
				checked = true;
			});

		InputCommand command;
		command.Player = 1;
		command.KeysDown[std::to_underlying(Key::Space)] = true;
		simulation.Step(std::span(&command, 1));
		CHECK(checked);
	}

	TEST_CASE("Advance runs the ticks that real time has made due")
	{
		Scene scene;
		Simulation simulation(scene, {.TickRate = 10, .MaxTicksPerFrame = 4});
		std::vector<uint64_t> sampledTicks;
		const auto sample = [&sampledTicks](uint64_t tick)
		{
			sampledTicks.push_back(tick);
			InputCommand command;
			command.Tick = tick;
			return std::vector<InputCommand>{command};
		};

		CHECK(simulation.Advance(0.05, sample) == 0);
		CHECK(simulation.GetInterpolationFactor() == doctest::Approx(0.5f));
		CHECK(simulation.Advance(0.06, sample) == 1);
		CHECK(simulation.GetInterpolationFactor() == doctest::Approx(0.1f));
		CHECK(simulation.Advance(0.25, sample) == 2);
		CHECK(sampledTicks == std::vector<uint64_t>{0, 1, 2});
		CHECK(simulation.GetTick() == 3);

		SUBCASE("Bad time measurements add nothing")
		{
			CHECK(simulation.Advance(-1.0, sample) == 0);
			CHECK(simulation.Advance(std::numeric_limits<double>::quiet_NaN(), sample) == 0);
			CHECK(simulation.Advance(std::numeric_limits<double>::infinity(), sample) == 0);
			CHECK(simulation.GetTick() == 3);
		}
		SUBCASE("A long frame runs at most MaxTicksPerFrame ticks, and the rest of it is skipped")
		{
			CHECK(simulation.Advance(10.0, sample) == 4);
			CHECK(simulation.Advance(0.0, sample) == 0);
			CHECK(simulation.GetInterpolationFactor() < 1.0f);
		}
	}

	TEST_CASE("Rendering interpolates transforms between the start and end of the latest tick")
	{
		Scene scene;
		Entity entity = scene.CreateEntity();
		Simulation simulation(scene, {.TickRate = 10});
		simulation.AddSystem("Move",
			[&entity](SimulationContext&)
			{
				entity.GetTransform().Position.x += 1.0f;
				entity.GetTransform().Scale *= 2.0f;
			});
		const auto noInput = [](uint64_t) { return std::vector<InputCommand>(); };

		// Before any tick, the entity is where it is
		CHECK(simulation.GetInterpolatedTransform(entity).Position.x == 0.0f);

		CHECK(simulation.Advance(0.125, noInput) == 1);
		const TransformComponent interpolated = simulation.GetInterpolatedTransform(entity);
		// A quarter of the way from the tick's start (x = 0, scale 1) to its end (x = 1, scale 2)
		CHECK(interpolated.Position.x == doctest::Approx(0.25f));
		CHECK(interpolated.Scale.y == doctest::Approx(1.25f));

		// Entities created since the latest tick render where they are
		Entity created = scene.CreateEntity();
		created.GetTransform().Position.z = 3.0f;
		CHECK(simulation.GetInterpolatedTransform(created).Position.z == 3.0f);
	}

	TEST_CASE("Input commands answer queries about keys, buttons and axes")
	{
		InputCommand command;
		command.KeysDown[std::to_underlying(Key::LeftShift)] = true;
		command.KeysPressed[std::to_underlying(Key::E)] = true;
		command.KeysReleased[std::to_underlying(Key::Q)] = true;
		command.MouseButtonsPressed[std::to_underlying(MouseButton::Left)] = true;
		command.GamepadButtonsDown[std::to_underlying(GamepadButton::A)] = true;
		command.GamepadAxes[std::to_underlying(GamepadAxis::LeftX)] = -0.5f;

		CHECK(command.IsKeyDown(Key::LeftShift));
		CHECK(command.WasKeyPressed(Key::E));
		CHECK(command.WasKeyReleased(Key::Q));
		CHECK_FALSE(command.IsKeyDown(Key::E));
		CHECK(command.WasMouseButtonPressed(MouseButton::Left));
		CHECK_FALSE(command.IsMouseButtonDown(MouseButton::Left));
		CHECK(command.IsGamepadButtonDown(GamepadButton::A));
		CHECK(command.GetGamepadAxis(GamepadAxis::LeftX) == -0.5f);
		CHECK(command == command);
		CHECK_FALSE(command == InputCommand());
	}

}
