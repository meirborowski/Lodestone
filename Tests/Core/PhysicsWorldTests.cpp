#include "Lodestone/Physics/PhysicsWorld.h"

#include "Lodestone/Asset/Model.h"
#include "Lodestone/Input/InputCommand.h"
#include "Lodestone/Physics/PhysicsComponents.h"
#include "Lodestone/Scene/Entity.h"
#include "Lodestone/Scene/SceneSerializer.h"
#include "Lodestone/Simulation/Simulation.h"

#include <doctest/doctest.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Lodestone {

	namespace {

		// A scene with a physics world, simulated at 60 Hz, with a hook that runs before physics each tick, as
		// gameplay would
		class PhysicsTest
		{
		public:
			explicit PhysicsTest(PhysicsWorldConfig config = {.WorkerThreads = 0})
				: m_Physics(m_Scene, std::move(config)), m_Simulation(m_Scene, {.TickRate = 60})
			{
				m_Simulation.AddSystem("Gameplay",
					[this](SimulationContext& context)
					{
						if (m_Gameplay)
							m_Gameplay(context);
					});
				m_Simulation.AddSystem("Physics", m_Physics);
			}

			Scene& GetScene() { return m_Scene; }
			PhysicsWorld& GetPhysics() { return m_Physics; }
			Simulation& GetSimulation() { return m_Simulation; }
			void SetGameplay(std::function<void(SimulationContext&)> gameplay) { m_Gameplay = std::move(gameplay); }

			// Runs ticks with player 0's input (without a tick or player: they're filled in), collecting the events
			std::vector<PhysicsEvent> Run(int ticks, const InputCommand& input = {})
			{
				std::vector<PhysicsEvent> events;
				for (int tick = 0; tick < ticks; ++tick)
				{
					InputCommand command = input;
					command.Tick = m_Simulation.GetTick();
					command.Player = 0;
					const std::vector<InputCommand> commands{command};
					m_Simulation.Step(commands);
					events.insert(events.end(), m_Physics.GetEvents().begin(), m_Physics.GetEvents().end());
				}
				return events;
			}

		private:
			Scene m_Scene;
			PhysicsWorld m_Physics;
			Simulation m_Simulation;
			std::function<void(SimulationContext&)> m_Gameplay;
		};

		Entity AddEntity(Scene& scene, std::string_view name, const glm::vec3& position,
			const glm::quat& rotation = glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f),
			const glm::vec3& scale = glm::vec3(1.0f))
		{
			Entity entity = scene.CreateEntity(name);
			auto& transform = entity.Get<TransformComponent>();
			transform.Position = position;
			transform.Rotation = rotation;
			transform.Scale = scale;
			return entity;
		}

		// Static ground whose top is at the height
		Entity AddFloor(Scene& scene, float height = 0.0f)
		{
			Entity floor = AddEntity(scene, "Floor", glm::vec3(0.0f, height - 0.5f, 0.0f));
			floor.Add<BoxColliderComponent>(BoxColliderComponent{.Size = glm::vec3(100.0f, 1.0f, 100.0f)});
			return floor;
		}

		Entity AddBox(Scene& scene, const glm::vec3& position, BodyType type = BodyType::Dynamic,
			const glm::vec3& size = glm::vec3(1.0f))
		{
			Entity box = AddEntity(scene, "Box", position);
			box.Add<RigidBodyComponent>(RigidBodyComponent{.Type = static_cast<uint32_t>(type)});
			box.Add<BoxColliderComponent>(BoxColliderComponent{.Size = size});
			return box;
		}

		Entity AddCharacter(Scene& scene, const glm::vec3& position)
		{
			Entity character = AddEntity(scene, "Player", position);
			character.Add<CharacterControllerComponent>();
			return character;
		}

		void SetGravity(Scene& scene, const glm::vec3& gravity)
		{
			scene.CreateEntity("Settings").Add<PhysicsSettingsComponent>(PhysicsSettingsComponent{.Gravity = gravity});
		}

		glm::vec3 GetPosition(const Entity& entity)
		{
			return entity.Get<TransformComponent>().Position;
		}

		bool IsNear(const glm::vec3& value, const glm::vec3& expected, float tolerance)
		{
			return glm::all(glm::lessThanEqual(glm::abs(value - expected), glm::vec3(tolerance)));
		}

		// Whether a body rests where expected: on the surface below it, sunk no deeper than Jolt lets resting bodies
		// sink into each other (its penetration slop, 2 cm)
		bool IsResting(const glm::vec3& position, const glm::vec3& expected)
		{
			constexpr float slop = 0.02f;
			return std::abs(position.x - expected.x) <= slop && std::abs(position.z - expected.z) <= slop &&
				position.y >= expected.y - slop - 0.002f && position.y <= expected.y + 0.002f;
		}

		size_t CountEvents(const std::vector<PhysicsEvent>& events, PhysicsEventType type, UUID first, UUID second)
		{
			return static_cast<size_t>(std::ranges::count_if(events,
				[&](const PhysicsEvent& event)
				{
					return event.Type == type &&
						((event.First == first && event.Second == second) ||
							(event.First == second && event.Second == first));
				}));
		}

		// A model with one mesh: a square of triangles lying flat at y = 0, the size given, or a cube
		Model MakeQuadModel(float size)
		{
			const float half = 0.5f * size;
			ModelMesh mesh;
			for (const glm::vec3& corner : {glm::vec3(-half, 0.0f, -half), glm::vec3(-half, 0.0f, half),
					 glm::vec3(half, 0.0f, half), glm::vec3(half, 0.0f, -half)})
				mesh.Vertices.push_back({.Position = corner});
			mesh.Indices = {0, 1, 2, 0, 2, 3};
			Model model;
			model.Meshes.push_back(std::move(mesh));
			return model;
		}

		Model MakeCubeModel(float size)
		{
			const float half = 0.5f * size;
			ModelMesh mesh;
			for (int corner = 0; corner < 8; ++corner)
				mesh.Vertices.push_back({.Position = glm::vec3((corner & 1) != 0 ? half : -half,
											 (corner & 2) != 0 ? half : -half, (corner & 4) != 0 ? half : -half)});
			mesh.Indices = {0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4, 2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1,
				3, 5, 3, 7, 5};
			Model model;
			model.Meshes.push_back(std::move(mesh));
			return model;
		}

		InputCommand Holding(Key key)
		{
			InputCommand input;
			input.KeysDown.set(std::to_underlying(key));
			return input;
		}

		InputCommand Pressing(Key key)
		{
			InputCommand input = Holding(key);
			input.KeysPressed.set(std::to_underlying(key));
			return input;
		}

	}

	TEST_CASE("Dynamic bodies fall under gravity and come to rest on static colliders")
	{
		PhysicsTest test;
		AddFloor(test.GetScene());
		const Entity box = AddBox(test.GetScene(), glm::vec3(0.0f, 5.0f, 0.0f));

		test.Run(30);
		// Half a second of falling from rest: g t^2 / 2, less a little damping
		CHECK(GetPosition(box).y == doctest::Approx(5.0f - 0.5f * 9.81f * 0.25f).epsilon(0.02));
		CHECK(box.Get<RigidBodyComponent>().LinearVelocity.y < -4.5f);

		test.Run(150);
		CHECK(IsResting(GetPosition(box), glm::vec3(0.0f, 0.5f, 0.0f)));
		CHECK(glm::length(box.Get<RigidBodyComponent>().LinearVelocity) < 0.05f);
		CHECK(test.GetPhysics().HasBody(box.GetId()));
		CHECK(test.GetPhysics().GetBodyCount() == 2);
	}

	TEST_CASE("Gravity comes from the scene's PhysicsSettings")
	{
		PhysicsTest test;
		SetGravity(test.GetScene(), glm::vec3(0.0f));
		const Entity floating = AddBox(test.GetScene(), glm::vec3(0.0f, 5.0f, 0.0f));
		test.Run(60);
		CHECK(GetPosition(floating) == glm::vec3(0.0f, 5.0f, 0.0f));

		for (auto [entity, settings] : test.GetScene().GetRegistry().view<PhysicsSettingsComponent>().each())
			settings.Gravity = glm::vec3(4.0f, 0.0f, 0.0f);
		test.Run(60);
		CHECK(GetPosition(floating).x > 1.5f);
		CHECK(GetPosition(floating).y == 5.0f);
	}

	TEST_CASE("Bodies move at the velocity set on them, and physics writes their velocity back")
	{
		PhysicsTest test;
		SetGravity(test.GetScene(), glm::vec3(0.0f));
		Entity box = AddBox(test.GetScene(), glm::vec3(0.0f));
		auto& body = box.Get<RigidBodyComponent>();
		body.LinearDamping = 0.0f;
		body.AngularDamping = 0.0f;
		body.LinearVelocity = glm::vec3(1.0f, 0.0f, 0.0f);
		body.AngularVelocity = glm::vec3(0.0f, 90.0f, 0.0f);

		test.Run(60);
		CHECK(IsNear(GetPosition(box), glm::vec3(1.0f, 0.0f, 0.0f), 1.0e-3f));
		CHECK(IsNear(box.Get<RigidBodyComponent>().LinearVelocity, glm::vec3(1.0f, 0.0f, 0.0f), 1.0e-4f));
		// A quarter turn around Y in a second
		const glm::vec3 forward = box.Get<TransformComponent>().Rotation * glm::vec3(0.0f, 0.0f, -1.0f);
		CHECK(IsNear(forward, glm::vec3(-1.0f, 0.0f, 0.0f), 0.01f));

		// Changing the component's velocity changes the body's
		box.Get<RigidBodyComponent>().LinearVelocity = glm::vec3(0.0f, 2.0f, 0.0f);
		test.Run(30);
		CHECK(IsNear(GetPosition(box), glm::vec3(1.0f, 1.0f, 0.0f), 1.0e-3f));
	}

	TEST_CASE("Moving a dynamic body's entity teleports it")
	{
		PhysicsTest test;
		AddFloor(test.GetScene());
		Entity box = AddBox(test.GetScene(), glm::vec3(0.0f, 0.5f, 0.0f));
		test.Run(10);

		box.Get<TransformComponent>().Position = glm::vec3(5.0f, 3.0f, 0.0f);
		test.Run(1);
		CHECK(GetPosition(box).x == doctest::Approx(5.0f));
		CHECK(GetPosition(box).y < 3.0f);
		CHECK(GetPosition(box).y > 2.9f);
		test.Run(120);
		CHECK(IsResting(GetPosition(box), glm::vec3(5.0f, 0.5f, 0.0f)));
	}

	TEST_CASE("Kinematic bodies follow their entities and push dynamic bodies")
	{
		PhysicsTest test;
		SetGravity(test.GetScene(), glm::vec3(0.0f));
		Entity pusher = AddBox(test.GetScene(), glm::vec3(0.0f), BodyType::Kinematic);
		const Entity pushed = AddBox(test.GetScene(), glm::vec3(2.0f, 0.0f, 0.0f));
		test.SetGameplay([&pusher](SimulationContext&) { pusher.Get<TransformComponent>().Position.x += 0.05f; });

		test.Run(40);
		CHECK(GetPosition(pusher).x == doctest::Approx(2.0f));
		CHECK(GetPosition(pushed).x > 2.9f);
		const auto hit = test.GetPhysics().Raycast(glm::vec3(2.0f, 5.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
		REQUIRE(hit.has_value());
		CHECK(hit.value_or(RaycastHit{}).Entity == pusher.GetId());

		// When it stops, it stays
		test.SetGameplay({});
		test.Run(30);
		CHECK(GetPosition(pusher).x == doctest::Approx(2.0f));
		const auto stopped = test.GetPhysics().Raycast(glm::vec3(2.0f, 5.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
		CHECK(stopped.value_or(RaycastHit{}).Entity == pusher.GetId());
		CHECK(stopped.value_or(RaycastHit{}).Point.y == doctest::Approx(0.5f).epsilon(0.001));
	}

	TEST_CASE("Static bodies follow their entities when they're moved")
	{
		PhysicsTest test;
		Entity floor = AddFloor(test.GetScene());
		test.Run(1);
		const glm::vec3 origin(0.0f, 10.0f, 0.0f);
		const glm::vec3 down(0.0f, -1.0f, 0.0f);
		CHECK(test.GetPhysics().Raycast(origin, down).value_or(RaycastHit{}).Point.y == doctest::Approx(0.0f));

		floor.Get<TransformComponent>().Position.y = 2.5f;
		test.Run(1);
		CHECK(test.GetPhysics().Raycast(origin, down).value_or(RaycastHit{}).Point.y == doctest::Approx(3.0f));
	}

	TEST_CASE("A body's colliders on it and its descendants make one shape, placed and scaled with their entities")
	{
		PhysicsTest test;
		SetGravity(test.GetScene(), glm::vec3(0.0f));
		Scene& scene = test.GetScene();
		Entity root = AddEntity(scene, "Table", glm::vec3(0.0f));
		root.Add<RigidBodyComponent>(RigidBodyComponent{.Type = static_cast<uint32_t>(BodyType::Kinematic)});
		root.Add<BoxColliderComponent>(
			BoxColliderComponent{.Size = glm::vec3(1.0f), .Center = glm::vec3(0.0f, 1.0f, 0.0f)});
		// A box doubled in size off to the side, and one stretched to three times its height on the other
		Entity side = AddEntity(
			scene, "Side", glm::vec3(4.0f, 0.0f, 0.0f), glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(2.0f));
		side.Add<BoxColliderComponent>();
		REQUIRE(scene.SetParent(side, root).has_value());
		Entity tall = AddEntity(scene, "Tall", glm::vec3(-4.0f, 0.0f, 0.0f), glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f),
			glm::vec3(1.0f, 3.0f, 1.0f));
		tall.Add<BoxColliderComponent>();
		REQUIRE(scene.SetParent(tall, root).has_value());
		test.Run(1);

		CHECK(test.GetPhysics().GetBodyCount() == 1);
		const glm::vec3 down(0.0f, -1.0f, 0.0f);
		const auto top = [&](float x)
		{ return test.GetPhysics().Raycast(glm::vec3(x, 10.0f, 0.0f), down).value_or(RaycastHit{}); };
		CHECK(top(0.0f).Point.y == doctest::Approx(1.5f));
		CHECK(top(0.0f).Entity == root.GetId());
		CHECK(top(4.0f).Point.y == doctest::Approx(1.0f));
		CHECK(top(4.0f).Entity == root.GetId());
		CHECK(top(-4.0f).Point.y == doctest::Approx(1.5f));

		// Moving the body moves its colliders; moving a collider's entity reshapes the body
		root.Get<TransformComponent>().Position.y = 1.0f;
		side.Get<TransformComponent>().Position.x = 6.0f;
		test.Run(1);
		CHECK(top(0.0f).Point.y == doctest::Approx(2.5f));
		CHECK(top(6.0f).Point.y == doctest::Approx(2.0f));
		CHECK_FALSE(test.GetPhysics().Raycast(glm::vec3(4.0f, 10.0f, 0.0f), down).has_value());
	}

	TEST_CASE("Colliders without a body above them are static bodies, and colliders below characters are ignored")
	{
		PhysicsTest test;
		Scene& scene = test.GetScene();
		AddEntity(scene, "Rock", glm::vec3(0.0f)).Add<SphereColliderComponent>();
		AddEntity(scene, "Pillar", glm::vec3(5.0f, 0.0f, 0.0f)).Add<CapsuleColliderComponent>();
		const Entity player = AddCharacter(scene, glm::vec3(-5.0f, 0.0f, 0.0f));
		Entity sword = AddEntity(scene, "Sword", glm::vec3(-5.0f, 1.0f, 0.0f));
		sword.Add<BoxColliderComponent>();
		REQUIRE(scene.SetParent(sword, player).has_value());
		test.Run(1);

		// The rock, the pillar and the player's inner body
		CHECK(test.GetPhysics().GetBodyCount() == 3);
		const glm::vec3 down(0.0f, -1.0f, 0.0f);
		CHECK(test.GetPhysics().Raycast(glm::vec3(0.0f, 10.0f, 0.0f), down).value_or(RaycastHit{}).Point.y ==
			doctest::Approx(0.5f));
		CHECK(test.GetPhysics().Raycast(glm::vec3(5.0f, 10.0f, 0.0f), down).value_or(RaycastHit{}).Point.y ==
			doctest::Approx(1.0f));
		CHECK_FALSE(test.GetPhysics().HasBody(sword.GetId()));
	}

	TEST_CASE("Mesh colliders collide as triangles, or as their convex hull")
	{
		const UUID groundModel = UUID::Generate();
		const UUID cubeModel = UUID::Generate();
		const Model ground = MakeQuadModel(20.0f);
		const Model cube = MakeCubeModel(1.0f);
		PhysicsTest test({
			.Models = [&](UUID id) -> const Model*
			{
				if (id == groundModel)
					return &ground;
				if (id == cubeModel)
					return &cube;
				return nullptr;
			},
			.WorkerThreads = 0,
		});
		Scene& scene = test.GetScene();
		AddEntity(scene, "Ground", glm::vec3(0.0f))
			.Add<MeshColliderComponent>(MeshColliderComponent{.Model = groundModel});
		Entity hull = AddEntity(scene, "Hull", glm::vec3(-2.0f, 3.0f, 0.0f));
		hull.Add<RigidBodyComponent>();
		hull.Add<MeshColliderComponent>(MeshColliderComponent{.Model = cubeModel, .Convex = true});
		// Dynamic bodies can't be triangle meshes: this one gets the hull too
		Entity triangles = AddEntity(scene, "Triangles", glm::vec3(2.0f, 3.0f, 0.0f));
		triangles.Add<RigidBodyComponent>();
		triangles.Add<MeshColliderComponent>(MeshColliderComponent{.Model = cubeModel});
		// A missing model leaves the body without a shape: it falls through everything
		Entity missing = AddEntity(scene, "Missing", glm::vec3(6.0f, 3.0f, 0.0f));
		missing.Add<RigidBodyComponent>();
		missing.Add<MeshColliderComponent>(MeshColliderComponent{.Model = UUID::Generate()});

		test.Run(180);
		CHECK(IsResting(GetPosition(hull), glm::vec3(-2.0f, 0.5f, 0.0f)));
		CHECK(IsResting(GetPosition(triangles), glm::vec3(2.0f, 0.5f, 0.0f)));
		CHECK(GetPosition(missing).y < -5.0f);
		CHECK(test.GetPhysics()
				  .Raycast(glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f))
				  .value_or(RaycastHit{})
				  .Point.y == doctest::Approx(0.0f));
	}

	TEST_CASE("Raycasts find the closest body, where it was hit, its normal and how far")
	{
		PhysicsTest test;
		Scene& scene = test.GetScene();
		const Entity floor = AddFloor(scene);
		const Entity box = AddBox(scene, glm::vec3(0.0f, 0.5f, 0.0f), BodyType::Static);
		Entity trigger = AddBox(scene, glm::vec3(0.0f, 5.0f, 0.0f), BodyType::Static);
		trigger.Get<RigidBodyComponent>().IsTrigger = true;
		const Entity player = AddCharacter(scene, glm::vec3(5.0f, 0.0f, 0.0f));
		test.Run(1);
		const PhysicsWorld& physics = test.GetPhysics();
		const glm::vec3 down(0.0f, -1.0f, 0.0f);

		const auto hit = physics.Raycast(glm::vec3(0.0f, 10.0f, 0.0f), down * 3.0f);
		REQUIRE(hit.has_value());
		CHECK(hit.value_or(RaycastHit{}).Entity == box.GetId());
		CHECK(IsNear(hit.value_or(RaycastHit{}).Point, glm::vec3(0.0f, 1.0f, 0.0f), 1.0e-4f));
		CHECK(IsNear(hit.value_or(RaycastHit{}).Normal, glm::vec3(0.0f, 1.0f, 0.0f), 1.0e-4f));
		CHECK(hit.value_or(RaycastHit{}).Distance == doctest::Approx(9.0f));

		CHECK(physics.Raycast(glm::vec3(0.0f, 10.0f, 0.0f), down, {.IncludeTriggers = true})
				  .value_or(RaycastHit{})
				  .Entity == trigger.GetId());
		CHECK(physics.Raycast(glm::vec3(0.0f, 10.0f, 0.0f), down, {.Ignore = box.GetId()})
				  .value_or(RaycastHit{})
				  .Entity == floor.GetId());
		CHECK_FALSE(physics.Raycast(glm::vec3(0.0f, 10.0f, 0.0f), down, {.MaxDistance = 8.0f}).has_value());
		CHECK_FALSE(physics.Raycast(glm::vec3(0.0f, 10.0f, 0.0f), -down).has_value());
		CHECK_FALSE(physics.Raycast(glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.0f)).has_value());
		// Characters are hit, by their capsule
		CHECK(physics.Raycast(glm::vec3(5.0f, 10.0f, 0.0f), down).value_or(RaycastHit{}).Entity == player.GetId());

		const std::vector<RaycastHit> all =
			physics.RaycastAll(glm::vec3(0.0f, 10.0f, 0.0f), down, {.IncludeTriggers = true});
		REQUIRE(all.size() == 3);
		CHECK(all[0].Entity == trigger.GetId());
		CHECK(all[1].Entity == box.GetId());
		CHECK(all[2].Entity == floor.GetId());
		CHECK(all[2].Distance == doctest::Approx(10.0f));
	}

	TEST_CASE("Raycasts that hit several bodies at the same distance always return the same one")
	{
		for (const uint32_t workers : {0u, 2u, 0u})
		{
			PhysicsTest test({.WorkerThreads = workers});
			std::vector<Entity> boxes;
			boxes.reserve(4);
			for (int i = 0; i < 4; ++i)
				boxes.push_back(AddBox(test.GetScene(), glm::vec3(0.0f), BodyType::Static));
			test.Run(1);
			const auto hit = test.GetPhysics().Raycast(glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
			REQUIRE(hit.has_value());
			// The first body made: the lowest body ID
			CHECK(hit.value_or(RaycastHit{}).Entity == boxes.front().GetId());
		}
	}

	TEST_CASE("Triggers detect bodies passing through them without stopping them")
	{
		PhysicsTest test;
		Scene& scene = test.GetScene();
		Entity trigger = AddBox(scene, glm::vec3(0.0f, 3.0f, 0.0f), BodyType::Static, glm::vec3(4.0f, 2.0f, 4.0f));
		trigger.Get<RigidBodyComponent>().IsTrigger = true;
		const Entity box = AddBox(scene, glm::vec3(0.0f, 8.0f, 0.0f));

		const std::vector<PhysicsEvent> events = test.Run(120);
		CHECK(GetPosition(box).y < -5.0f);
		CHECK(CountEvents(events, PhysicsEventType::TriggerEnter, trigger.GetId(), box.GetId()) == 1);
		CHECK(CountEvents(events, PhysicsEventType::TriggerExit, trigger.GetId(), box.GetId()) == 1);
		const auto enter = std::ranges::find(events, PhysicsEventType::TriggerEnter, &PhysicsEvent::Type);
		const auto exit = std::ranges::find(events, PhysicsEventType::TriggerExit, &PhysicsEvent::Type);
		CHECK(enter < exit);
		// The trigger comes first
		REQUIRE(enter != events.end());
		CHECK(enter->First == trigger.GetId());
		CHECK(std::ranges::none_of(
			events, [](const PhysicsEvent& event) { return event.Type == PhysicsEventType::CollisionBegin; }));
	}

	TEST_CASE("Bodies at rest stay touching and inside triggers when they fall asleep")
	{
		PhysicsTest test;
		Scene& scene = test.GetScene();
		const Entity floor = AddFloor(scene);
		Entity trigger = AddBox(scene, glm::vec3(0.0f, 1.0f, 0.0f), BodyType::Static, glm::vec3(4.0f, 2.0f, 4.0f));
		trigger.Get<RigidBodyComponent>().IsTrigger = true;
		Entity box = AddBox(scene, glm::vec3(0.0f, 4.0f, 0.0f));

		// Long enough to land and fall asleep
		const std::vector<PhysicsEvent> events = test.Run(600);
		CHECK(IsResting(GetPosition(box), glm::vec3(0.0f, 0.5f, 0.0f)));
		CHECK(CountEvents(events, PhysicsEventType::TriggerEnter, trigger.GetId(), box.GetId()) == 1);
		CHECK(CountEvents(events, PhysicsEventType::TriggerExit, trigger.GetId(), box.GetId()) == 0);
		CHECK(CountEvents(events, PhysicsEventType::CollisionBegin, floor.GetId(), box.GetId()) == 1);
		CHECK(CountEvents(events, PhysicsEventType::CollisionEnd, floor.GetId(), box.GetId()) == 0);

		// Lifted out, they end
		box.Get<TransformComponent>().Position.y = 20.0f;
		const std::vector<PhysicsEvent> lifted = test.Run(2);
		CHECK(CountEvents(lifted, PhysicsEventType::TriggerExit, trigger.GetId(), box.GetId()) == 1);
		CHECK(CountEvents(lifted, PhysicsEventType::CollisionEnd, floor.GetId(), box.GetId()) == 1);
	}

	TEST_CASE("Touching ends when a body goes away")
	{
		PhysicsTest test;
		Scene& scene = test.GetScene();
		const Entity floor = AddFloor(scene);
		const Entity box = AddBox(scene, glm::vec3(0.0f, 0.6f, 0.0f));
		const UUID boxId = box.GetId();
		const std::vector<PhysicsEvent> landed = test.Run(30);
		CHECK(CountEvents(landed, PhysicsEventType::CollisionBegin, floor.GetId(), boxId) == 1);

		scene.DestroyEntity(box);
		const std::vector<PhysicsEvent> gone = test.Run(1);
		CHECK(CountEvents(gone, PhysicsEventType::CollisionEnd, floor.GetId(), boxId) == 1);
		CHECK(test.GetPhysics().GetBodyCount() == 1);
	}

	TEST_CASE("Changing physics components changes the bodies")
	{
		PhysicsTest test;
		Scene& scene = test.GetScene();
		AddFloor(scene);
		Entity box = AddBox(scene, glm::vec3(0.0f, 0.5f, 0.0f), BodyType::Static);
		test.Run(1);
		const auto top = [&]
		{
			return test.GetPhysics()
				.Raycast(glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f))
				.value_or(RaycastHit{})
				.Point.y;
		};
		CHECK(top() == doctest::Approx(1.0f));

		box.Get<BoxColliderComponent>().Size = glm::vec3(1.0f, 3.0f, 1.0f);
		test.Run(1);
		CHECK(top() == doctest::Approx(2.0f));

		// Static to dynamic: it falls until it rests on the floor
		box.Get<RigidBodyComponent>().Type = static_cast<uint32_t>(BodyType::Dynamic);
		test.Run(120);
		CHECK(GetPosition(box).y == doctest::Approx(1.5f).epsilon(0.02));

		// Without a RigidBody its collider is a static body of its own
		box.Remove<RigidBodyComponent>();
		test.Run(1);
		CHECK(test.GetPhysics().HasBody(box.GetId()));
		CHECK(test.GetPhysics().GetBodyCount() == 2);
		box.Remove<BoxColliderComponent>();
		test.Run(1);
		CHECK_FALSE(test.GetPhysics().HasBody(box.GetId()));
		CHECK(test.GetPhysics().GetBodyCount() == 1);
	}

	TEST_CASE("Syncing a physics world without stepping it doesn't change the scene")
	{
		Scene scene;
		AddFloor(scene);
		Entity box = AddBox(scene, glm::vec3(0.0f, 5.0f, 0.0f));
		box.Get<RigidBodyComponent>().LinearVelocity = glm::vec3(1.0f, 0.0f, 0.0f);
		const std::string before = SceneSerializer::SerializeToText(scene);

		PhysicsWorld physics(scene, {.WorkerThreads = 0});
		physics.Sync();
		physics.Sync();

		CHECK(SceneSerializer::SerializeToText(scene) == before);
		CHECK(physics.GetBodyCount() == 2);
		CHECK(
			physics.Raycast(glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)).value_or(RaycastHit{}).Entity ==
			box.GetId());
	}

	TEST_CASE("Dynamic bodies below other entities are written relative to their parents")
	{
		PhysicsTest test;
		Scene& scene = test.GetScene();
		AddFloor(scene);
		const Entity holder = AddEntity(scene, "Holder", glm::vec3(10.0f, 2.0f, 0.0f),
			glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::vec3(2.0f));
		const Entity box = AddBox(scene, glm::vec3(0.0f));
		REQUIRE(scene.SetParent(box, holder).has_value());
		// Its local position is relative to the holder: it starts at the holder's position, two meters up
		test.Run(180);

		const glm::mat4 world = scene.GetWorldMatrix(box);
		CHECK(IsNear(glm::vec3(world[3]), glm::vec3(10.0f, 1.0f, 0.0f), 0.03f));
	}

	TEST_CASE("Character controllers stand on the ground, walk where they face, turn and jump")
	{
		PhysicsTest test;
		Scene& scene = test.GetScene();
		AddFloor(scene);
		const Entity player = AddCharacter(scene, glm::vec3(0.0f, 0.5f, 0.0f));

		test.Run(30);
		const auto& character = player.Get<CharacterControllerComponent>();
		CHECK(character.Grounded);
		CHECK(GetPosition(player).y == doctest::Approx(0.0f).epsilon(0.03));
		CHECK(test.GetPhysics().HasBody(player.GetId()));

		// Forward is -Z
		test.Run(60, Holding(Key::W));
		CHECK(GetPosition(player).z == doctest::Approx(-4.0f).epsilon(0.05));
		CHECK(std::abs(GetPosition(player).x) < 0.01f);

		// 900 pixels to the right at 0.1 degrees each: a quarter turn right, to face +X
		InputCommand turn;
		turn.MouseDelta.x = 900.0f;
		test.Run(1, turn);
		const glm::vec3 facing = player.Get<TransformComponent>().Rotation * glm::vec3(0.0f, 0.0f, -1.0f);
		CHECK(IsNear(facing, glm::vec3(1.0f, 0.0f, 0.0f), 1.0e-4f));
		const float startX = GetPosition(player).x;
		test.Run(30, Holding(Key::W));
		CHECK(GetPosition(player).x - startX == doctest::Approx(2.0f).epsilon(0.05));

		// The left stick walks too: down is positive, so pushing it up walks forward
		InputCommand stick;
		stick.GamepadConnected = true;
		stick.GamepadAxes[std::to_underlying(GamepadAxis::LeftY)] = -1.0f;
		const float stickStartX = GetPosition(player).x;
		test.Run(30, stick);
		CHECK(GetPosition(player).x - stickStartX == doctest::Approx(2.0f).epsilon(0.05));

		// A jump rises and lands again
		test.Run(1, Pressing(Key::Space));
		CHECK_FALSE(character.Grounded);
		CHECK(character.Velocity.y > 4.0f);
		float highest = 0.0f;
		for (int tick = 0; tick < 90; ++tick)
		{
			// Pressing again in the air doesn't jump again
			test.Run(1, tick == 10 ? Pressing(Key::Space) : InputCommand{});
			highest = std::max(highest, GetPosition(player).y);
		}
		// v^2 / 2g: about 1.27 meters
		CHECK(highest == doctest::Approx(25.0f / (2.0f * 9.81f)).epsilon(0.1));
		CHECK(character.Grounded);
		CHECK(GetPosition(player).y == doctest::Approx(0.0f).epsilon(0.03));
	}

	TEST_CASE("Character controllers are stopped by walls, walk up steps and push dynamic bodies")
	{
		PhysicsTest test;
		Scene& scene = test.GetScene();
		AddFloor(scene);
		AddBox(scene, glm::vec3(0.0f, 1.0f, -5.0f), BodyType::Static, glm::vec3(4.0f, 2.0f, 1.0f));
		const Entity walker = AddCharacter(scene, glm::vec3(0.0f));
		// A low step to the side, and one too high
		AddBox(scene, glm::vec3(10.0f, 0.1f, -11.0f), BodyType::Static, glm::vec3(4.0f, 0.2f, 20.0f));
		const Entity climber = AddCharacter(scene, glm::vec3(10.0f, 0.0f, 0.0f));
		AddBox(scene, glm::vec3(20.0f, 0.4f, -11.0f), BodyType::Static, glm::vec3(4.0f, 0.8f, 20.0f));
		const Entity blocked = AddCharacter(scene, glm::vec3(20.0f, 0.0f, 0.0f));
		const Entity crate = AddBox(scene, glm::vec3(30.0f, 0.5f, -2.0f));
		const Entity pusher = AddCharacter(scene, glm::vec3(30.0f, 0.0f, 0.0f));

		// Every character is player 0's
		test.Run(120, Holding(Key::W));
		// The wall's near face is at z = -4.5; the capsule's radius keeps the character off it
		CHECK(GetPosition(walker).z > -4.25f);
		CHECK(GetPosition(walker).z < -4.0f);
		CHECK(GetPosition(climber).y == doctest::Approx(0.2f).epsilon(0.03));
		CHECK(GetPosition(climber).z < -2.0f);
		CHECK(GetPosition(blocked).y < 0.05f);
		CHECK(GetPosition(blocked).z > -1.5f);
		CHECK(GetPosition(crate).z < -3.0f);
		CHECK(GetPosition(pusher).z < -2.0f);
	}

	TEST_CASE("Triggers detect characters, and characters collide with what they touch")
	{
		PhysicsTest test;
		Scene& scene = test.GetScene();
		const Entity floor = AddFloor(scene);
		Entity trigger = AddBox(scene, glm::vec3(0.0f, 1.0f, -3.0f), BodyType::Static, glm::vec3(2.0f, 2.0f, 2.0f));
		trigger.Get<RigidBodyComponent>().IsTrigger = true;
		const Entity player = AddCharacter(scene, glm::vec3(0.0f, 0.2f, 0.0f));

		const std::vector<PhysicsEvent> landing = test.Run(30);
		CHECK(CountEvents(landing, PhysicsEventType::CollisionBegin, floor.GetId(), player.GetId()) == 1);
		const std::vector<PhysicsEvent> walking = test.Run(90, Holding(Key::W));
		CHECK(CountEvents(walking, PhysicsEventType::TriggerEnter, trigger.GetId(), player.GetId()) == 1);
		CHECK(CountEvents(walking, PhysicsEventType::TriggerExit, trigger.GetId(), player.GetId()) == 1);
		// Walking along the floor never stops touching it
		CHECK(CountEvents(walking, PhysicsEventType::CollisionEnd, floor.GetId(), player.GetId()) == 0);
	}

	TEST_CASE("A character's velocity set on its component is used")
	{
		PhysicsTest test;
		AddFloor(test.GetScene());
		Entity player = AddCharacter(test.GetScene(), glm::vec3(0.0f));
		test.Run(10);
		player.Get<CharacterControllerComponent>().Velocity = glm::vec3(0.0f, 6.0f, 0.0f);
		test.Run(10);
		CHECK(GetPosition(player).y > 0.5f);
		CHECK_FALSE(player.Get<CharacterControllerComponent>().Grounded);
	}

	TEST_CASE("Bodies whose colliders are on their descendants come to rest and fall asleep")
	{
		PhysicsTest test;
		Scene& scene = test.GetScene();
		AddFloor(scene);
		// As a model instance would be: the body on the root, its collider on a node below, turned and offset
		Entity root = AddEntity(scene, "Crate", glm::vec3(0.0f, 3.0f, 0.0f),
			glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
		root.Add<RigidBodyComponent>();
		Entity node = AddEntity(scene, "Node", glm::vec3(0.5f, 0.0f, 0.0f),
			glm::angleAxis(glm::radians(15.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
		node.Add<BoxColliderComponent>();
		REQUIRE(scene.SetParent(node, root).has_value());

		test.Run(300);
		CHECK(test.GetPhysics().IsSleeping(root.GetId()));
		CHECK(scene.GetWorldMatrix(node)[3].y == doctest::Approx(0.5f).epsilon(0.03));
		CHECK_FALSE(test.GetPhysics().IsSleeping(node.GetId()));
	}

	TEST_CASE("Moving or removing a static body wakes the bodies asleep on it")
	{
		PhysicsTest test;
		Scene& scene = test.GetScene();
		Entity floor = AddFloor(scene);
		const Entity box = AddBox(scene, glm::vec3(0.0f, 0.5f, 0.0f));
		const Entity other = AddBox(scene, glm::vec3(10.0f, 0.5f, 0.0f));
		test.Run(120);
		REQUIRE(test.GetPhysics().IsSleeping(box.GetId()));

		// The floor drops a meter: the box falls onto it again, which ends and begins their touching
		floor.Get<TransformComponent>().Position.y -= 1.0f;
		const std::vector<PhysicsEvent> dropped = test.Run(90);
		CHECK(GetPosition(box).y == doctest::Approx(-0.5f).epsilon(0.03));
		CHECK(CountEvents(dropped, PhysicsEventType::CollisionEnd, floor.GetId(), box.GetId()) == 1);
		CHECK(CountEvents(dropped, PhysicsEventType::CollisionBegin, floor.GetId(), box.GetId()) == 1);

		// Without the floor, everything on it falls
		test.Run(120);
		REQUIRE(test.GetPhysics().IsSleeping(other.GetId()));
		scene.DestroyEntity(floor);
		test.Run(30);
		CHECK(GetPosition(other).y < -1.0f);
		CHECK(GetPosition(box).y < -1.0f);
	}

	TEST_CASE("Characters keep their momentum in the air, steered toward walking by their air acceleration")
	{
		PhysicsTest test;
		Scene& scene = test.GetScene();
		AddFloor(scene);
		Entity drifting = AddCharacter(scene, glm::vec3(0.0f));
		drifting.Get<CharacterControllerComponent>().AirAcceleration = 0.0f;
		const Entity steered = AddCharacter(scene, glm::vec3(10.0f, 0.0f, 0.0f));
		test.Run(10);
		test.Run(30, Holding(Key::W));

		// Running forward at 4 m/s, both jump, then let go of W at once
		InputCommand jump = Pressing(Key::Space);
		jump.KeysDown.set(std::to_underlying(Key::W));
		test.Run(1, jump);
		const float driftStart = GetPosition(drifting).z;
		const float steerStart = GetPosition(steered).z;
		test.Run(20);
		CHECK_FALSE(drifting.Get<CharacterControllerComponent>().Grounded);
		// Without air control, a third of a second at 4 m/s
		CHECK(driftStart - GetPosition(drifting).z == doctest::Approx(4.0f * 20.0f / 60.0f).epsilon(0.02));
		CHECK(drifting.Get<CharacterControllerComponent>().Velocity.z == doctest::Approx(-4.0f).epsilon(0.001));
		// With 10 m/s^2, it slows to a stop in 0.4 seconds: v^2 / 2a = 0.8 m, but only 0.33 s have passed
		const float steeredDistance = steerStart - GetPosition(steered).z;
		CHECK(steeredDistance > 0.5f);
		CHECK(steeredDistance < 4.0f * 20.0f / 60.0f);
		CHECK(steered.Get<CharacterControllerComponent>().Velocity.z > -1.0f);
	}

	TEST_CASE("A character shorter than twice its radius is a sphere standing on its position")
	{
		PhysicsTest test;
		AddFloor(test.GetScene());
		Entity ball = AddCharacter(test.GetScene(), glm::vec3(0.0f, 0.5f, 0.0f));
		ball.Get<CharacterControllerComponent>().Height = 0.5f;
		ball.Get<CharacterControllerComponent>().Radius = 0.4f;

		test.Run(60);
		CHECK(ball.Get<CharacterControllerComponent>().Grounded);
		CHECK(GetPosition(ball).y == doctest::Approx(0.0f).epsilon(0.03));
		// Its top is half a meter up: the sphere's radius is half its height, not the radius asked for
		const auto top =
			test.GetPhysics().Raycast(GetPosition(ball) + glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
		CHECK(top.value_or(RaycastHit{}).Point.y == doctest::Approx(0.5f).epsilon(0.03));
	}
	TEST_CASE("Restoring a saved state and re-simulating the same input commands reproduces the same physics state")
	{
		PhysicsTest test({.WorkerThreads = 2});
		Scene& scene = test.GetScene();
		AddFloor(scene);
		AddBox(scene, glm::vec3(0.0f, 1.0f, -8.0f), BodyType::Static, glm::vec3(6.0f, 0.5f, 3.0f))
			.Get<TransformComponent>()
			.Rotation = glm::angleAxis(glm::radians(20.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		std::vector<Entity> stack;
		stack.reserve(5);
		for (int level = 0; level < 5; ++level)
			stack.push_back(AddBox(
				scene, glm::vec3(0.1f * static_cast<float>(level), 0.5f + static_cast<float>(level) * 1.01f, -8.0f)));
		for (int i = 0; i < 4; ++i)
		{
			Entity ball =
				AddEntity(scene, "Ball", glm::vec3(-3.0f + static_cast<float>(i), 4.0f + static_cast<float>(i), -6.0f));
			ball.Add<RigidBodyComponent>(RigidBodyComponent{.Restitution = 0.6f});
			ball.Add<SphereColliderComponent>(SphereColliderComponent{.Radius = 0.3f});
		}
		Entity pill = AddEntity(scene, "Pill", glm::vec3(3.0f, 2.0f, -5.0f));
		pill.Add<RigidBodyComponent>();
		pill.Add<CapsuleColliderComponent>(CapsuleColliderComponent{.Height = 1.5f, .Radius = 0.3f});
		Entity platform = AddBox(scene, glm::vec3(0.0f, 0.25f, 4.0f), BodyType::Kinematic, glm::vec3(3.0f, 0.5f, 3.0f));
		Entity trigger = AddBox(scene, glm::vec3(0.0f, 1.0f, -3.0f), BodyType::Static, glm::vec3(3.0f, 2.0f, 3.0f));
		trigger.Get<RigidBodyComponent>().IsTrigger = true;
		AddCharacter(scene, glm::vec3(0.0f, 0.0f, 0.0f));

		// Gameplay that changes the world after the save: a platform moving, a body spawned and one destroyed, and
		// friction changed
		constexpr uint64_t saveTick = 60;
		test.SetGameplay(
			[&](SimulationContext& context)
			{
				const uint64_t tick = context.GetTick();
				platform.Get<TransformComponent>().Position.x = 2.0f * std::sin(static_cast<float>(tick) * 0.05f);
				if (tick == saveTick + 5)
					AddBox(context.GetScene(), glm::vec3(1.0f, 6.0f, -8.0f));
				if (tick == saveTick + 10)
					context.GetScene().DestroyEntity(stack[4]);
				if (tick == saveTick + 3)
					stack[0].Get<RigidBodyComponent>().Friction = 0.05f;
			});
		// Walking, turning and jumping, by tick
		const auto inputFor = [](uint64_t tick)
		{
			InputCommand input = Holding(tick % 90 < 60 ? Key::W : Key::D);
			input.MouseDelta.x = tick % 40 < 10 ? 25.0f : 0.0f;
			if (tick % 70 == 0)
			{
				input.KeysPressed.set(std::to_underlying(Key::Space));
				input.KeysDown.set(std::to_underlying(Key::Space));
			}
			return input;
		};
		const auto run = [&](uint64_t until)
		{
			std::vector<PhysicsEvent> events;
			while (test.GetSimulation().GetTick() < until)
			{
				const InputCommand input = inputFor(test.GetSimulation().GetTick());
				const std::vector<PhysicsEvent> tickEvents = test.Run(1, input);
				events.insert(events.end(), tickEvents.begin(), tickEvents.end());
			}
			return events;
		};
		const auto physicsState = [&]
		{
			std::vector<std::byte> state;
			test.GetPhysics().SaveState(state);
			return state;
		};

		run(saveTick);
		const SimulationState saved = test.GetSimulation().SaveState();
		const std::vector<PhysicsEvent> firstEvents = run(saveTick + 120);
		const std::string firstScene = SceneSerializer::SerializeToText(scene);
		const std::vector<std::byte> firstPhysics = physicsState();
		CHECK_FALSE(firstEvents.empty());

		test.GetSimulation().RestoreState(saved);
		const std::vector<PhysicsEvent> secondEvents = run(saveTick + 120);
		CHECK(SceneSerializer::SerializeToText(scene) == firstScene);
		CHECK(physicsState() == firstPhysics);
		CHECK(secondEvents == firstEvents);

		// And once more from the same state, which must be untouched by being restored
		test.GetSimulation().RestoreState(saved);
		CHECK(run(saveTick + 120) == firstEvents);
		CHECK(SceneSerializer::SerializeToText(scene) == firstScene);
	}

	TEST_CASE("Physics results don't depend on how many threads step the world")
	{
		const auto simulate = [](uint32_t workers)
		{
			PhysicsTest test({.WorkerThreads = workers});
			AddFloor(test.GetScene());
			std::vector<Entity> boxes;
			boxes.reserve(30);
			for (int i = 0; i < 30; ++i)
				boxes.push_back(AddBox(test.GetScene(),
					glm::vec3(0.37f * static_cast<float>(i % 5), 1.0f + 1.1f * static_cast<float>(i),
						0.21f * static_cast<float>(i % 3))));
			test.Run(240);
			std::vector<float> state;
			for (const Entity& box : boxes)
			{
				const auto& transform = box.Get<TransformComponent>();
				state.insert(state.end(),
					{transform.Position.x, transform.Position.y, transform.Position.z, transform.Rotation.x,
						transform.Rotation.y, transform.Rotation.z, transform.Rotation.w});
			}
			return state;
		};
		const std::vector<float> single = simulate(0);
		CHECK(simulate(3) == single);
		CHECK(simulate(1) == single);
	}

}
