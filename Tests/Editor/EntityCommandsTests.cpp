#include "Lodestone/Editor/Commands/EntityCommands.h"

#include "Common/DescribeError.h"
#include "Lodestone/Editor/Commands/CommandHistory.h"
#include "Lodestone/Scene/Entity.h"
#include "Lodestone/Scene/EntitySerializer.h"
#include "Lodestone/Scene/PrefabSerializer.h"

#include <doctest/doctest.h>

#include <span>
#include <string>
#include <vector>

namespace Lodestone {

	namespace {

		struct HealthComponent
		{
			int32_t Points = 100;
			UUID LastAttacker;
		};

		struct BrainComponent
		{
			float Thoughts = 0.0f;
		};

		const ComponentRegistry& GetTestComponents()
		{
			static const ComponentRegistry* s_Registry = []
			{
				auto* registry = new ComponentRegistry();
				RegisterCoreComponents(*registry);
				registry->Register<HealthComponent>("Health", "Hit points")
					.Field("Points", &HealthComponent::Points, {.Min = 0.0, .Max = 1000.0})
					.Field("LastAttacker", &HealthComponent::LastAttacker);
				registry->Register<BrainComponent>("Brain", {}, ComponentFlags::Internal)
					.Field("Thoughts", &BrainComponent::Thoughts);
				return registry;
			}();
			return *s_Registry;
		}

		// A scene, the history that edits it, and a hierarchy: Player (with Sword and Shield) and Enemy
		struct Fixture
		{
			Fixture()
				: Scene(GetTestComponents())
			{
				Entity player = Scene.CreateEntity("Player");
				const Entity sword = Scene.CreateEntity("Sword");
				const Entity shield = Scene.CreateEntity("Shield");
				const Entity enemy = Scene.CreateEntity("Enemy");
				REQUIRE(Scene.SetParent(sword, player).has_value());
				REQUIRE(Scene.SetParent(shield, player).has_value());
				player.GetTransform().Position = glm::vec3(1.0f, 2.0f, 3.0f);
				player.Add<HealthComponent>(HealthComponent{.Points = 80, .LastAttacker = enemy.GetId()});
				Player = player.GetId();
				Sword = sword.GetId();
				Shield = shield.GetId();
				Enemy = enemy.GetId();
			}

			std::expected<void, Error> Run(Scope<Command> command)
			{
				return History.Execute(Scene, std::move(command));
			}

			// The scene as a file would hold it, to compare states
			Json::Value Snapshot() const { return EntitySerializer::SerializeScene(Scene); }

			// Runs a command, then checks that undo restores the scene exactly and redo repeats the result exactly
			void CheckUndoRedo(Scope<Command> command)
			{
				const Json::Value before = Snapshot();
				const auto executed = Run(std::move(command));
				REQUIRE_MESSAGE(executed.has_value(), Testing::DescribeError(executed));
				const Json::Value after = Snapshot();
				CHECK(after != before);

				REQUIRE(History.Undo(Scene).has_value());
				CHECK(Snapshot() == before);
				REQUIRE(History.Redo(Scene).has_value());
				CHECK(Snapshot() == after);
				REQUIRE(History.Undo(Scene).has_value());
				CHECK(Snapshot() == before);
				REQUIRE(History.Redo(Scene).has_value());
			}

			// Runs a command that must fail, and checks that it changed nothing
			Error CheckFails(Scope<Command> command)
			{
				const Json::Value before = Snapshot();
				const auto executed = Run(std::move(command));
				REQUIRE_FALSE(executed.has_value());
				CHECK(Snapshot() == before);
				return executed.error();
			}

			Entity Get(UUID id) { return Scene.FindEntity(id); }

			Lodestone::Scene Scene;
			CommandHistory History;
			UUID Player;
			UUID Sword;
			UUID Shield;
			UUID Enemy;
		};

		std::vector<std::string> GetChildNames(Fixture& fixture, UUID parent)
		{
			const std::span<const UUID> children = parent.IsNil()
				? fixture.Scene.GetRootEntities()
				: std::span<const UUID>(fixture.Get(parent).Get<HierarchyComponent>().Children);
			std::vector<std::string> names;
			for (const UUID child : children)
				names.push_back(fixture.Get(child).GetName());
			return names;
		}

	}

	TEST_CASE("Creating an entity, with components and a parent, undoes and redoes with the same UUID")
	{
		Fixture fixture;
		const Json::Value components = {{"Transform", {{"Position", {4.0, 5.0, 6.0}}}}, {"Health", {{"Points", 7}}}};
		auto command = CreateScope<CreateEntityCommand>("Helmet", fixture.Player, 0, components);
		const CreateEntityCommand& create = *command;

		fixture.CheckUndoRedo(std::move(command));

		const Entity helmet = fixture.Get(create.GetEntityId());
		REQUIRE(helmet);
		CHECK(create.GetName() == "Create entity 'Helmet'");
		CHECK(helmet.GetName() == "Helmet");
		CHECK(helmet.GetTransform().Position == glm::vec3(4.0f, 5.0f, 6.0f));
		CHECK(helmet.Get<HealthComponent>().Points == 7);
		CHECK(GetChildNames(fixture, fixture.Player) == std::vector<std::string>{"Helmet", "Sword", "Shield"});
	}

	TEST_CASE("Creating an entity fails for bad components or parents, changing nothing")
	{
		Fixture fixture;
		const UUID missing = UUID::Generate();

		CHECK(fixture.CheckFails(CreateScope<CreateEntityCommand>("A", missing)).GetCode() == ErrorCode::NotFound);
		fixture.CheckFails(CreateScope<CreateEntityCommand>("B", UUID(), std::nullopt, Json::Value::array()));
		fixture.CheckFails(CreateScope<CreateEntityCommand>(
			"C", UUID(), std::nullopt, Json::Value{{"Unknown", Json::Value::object()}}));
		fixture.CheckFails(
			CreateScope<CreateEntityCommand>("D", UUID(), std::nullopt, Json::Value{{"Brain", Json::Value::object()}}));
		fixture.CheckFails(
			CreateScope<CreateEntityCommand>("E", UUID(), std::nullopt, Json::Value{{"Health", {{"Points", -1}}}}));
		fixture.CheckFails(
			CreateScope<CreateEntityCommand>("F", UUID(), std::nullopt, Json::Value{{"Health", {{"Points", "many"}}}}));
		// Read-only fields: the UUID and the parent can't be set through components
		fixture.CheckFails(CreateScope<CreateEntityCommand>(
			"G", UUID(), std::nullopt, Json::Value{{"ID", {{"ID", UUID::Generate().ToString()}}}}));
		fixture.CheckFails(CreateScope<CreateEntityCommand>(
			"H", UUID(), std::nullopt, Json::Value{{"Hierarchy", {{"Parent", fixture.Player.ToString()}}}}));
		CHECK_FALSE(fixture.History.CanUndo());
	}

	TEST_CASE("Deleting an entity takes its descendants, and undo restores them in place")
	{
		Fixture fixture;

		fixture.CheckUndoRedo(CreateScope<DestroyEntityCommand>(fixture.Sword));
		CHECK_FALSE(fixture.Scene.Contains(fixture.Sword));
		CHECK(fixture.History.GetUndoName() == "Delete entity 'Sword'");

		fixture.CheckUndoRedo(CreateScope<DestroyEntityCommand>(fixture.Player));
		CHECK(fixture.Scene.GetEntityCount() == 1);

		// Back to the start, with every entity where it was
		REQUIRE(fixture.History.Undo(fixture.Scene).has_value());
		REQUIRE(fixture.History.Undo(fixture.Scene).has_value());
		CHECK(GetChildNames(fixture, fixture.Player) == std::vector<std::string>{"Sword", "Shield"});
		CHECK(fixture.Get(fixture.Player).Get<HealthComponent>().LastAttacker == fixture.Enemy);
	}

	TEST_CASE("Deleting a missing entity fails")
	{
		Fixture fixture;
		CHECK(fixture.CheckFails(CreateScope<DestroyEntityCommand>(UUID::Generate())).GetCode() == ErrorCode::NotFound);
	}

	TEST_CASE("Duplicating an entity copies its tree next to it, and redo recreates the same copy")
	{
		Fixture fixture;
		auto command = CreateScope<DuplicateEntityCommand>(fixture.Player);
		const DuplicateEntityCommand& duplicate = *command;

		fixture.CheckUndoRedo(std::move(command));

		const Entity copy = fixture.Get(duplicate.GetCopyId());
		REQUIRE(copy);
		CHECK(copy.GetId() != fixture.Player);
		CHECK(GetChildNames(fixture, UUID()) == std::vector<std::string>{"Player", "Player", "Enemy"});
		CHECK(GetChildNames(fixture, copy.GetId()) == std::vector<std::string>{"Sword", "Shield"});
		CHECK(copy.Get<HealthComponent>().Points == 80);
		CHECK(fixture.Scene.GetEntityCount() == 7);
	}

	TEST_CASE("Instancing a prefab copies it with new UUIDs, and redo recreates the same instance")
	{
		Fixture fixture;
		const Json::Value prefab = PrefabSerializer::Serialize(fixture.Scene, fixture.Get(fixture.Player))["entities"];
		const UUID asset = UUID::Generate();
		auto command = CreateScope<InstantiatePrefabCommand>(prefab, asset, "Hero", fixture.Enemy);
		const InstantiatePrefabCommand& instantiate = *command;

		fixture.CheckUndoRedo(std::move(command));

		const Entity instance = fixture.Get(instantiate.GetInstanceId());
		REQUIRE(instance);
		CHECK(instantiate.GetName() == "Instance prefab 'Hero'");
		CHECK(instance.Get<HierarchyComponent>().Parent == fixture.Enemy);
		CHECK(instance.Get<PrefabInstanceComponent>().Prefab == asset);
		CHECK(GetChildNames(fixture, instance.GetId()) == std::vector<std::string>{"Sword", "Shield"});
	}

	TEST_CASE("Instancing a prefab under a missing parent fails")
	{
		Fixture fixture;
		const Json::Value prefab = PrefabSerializer::Serialize(fixture.Scene, fixture.Get(fixture.Sword))["entities"];

		fixture.CheckFails(CreateScope<InstantiatePrefabCommand>(prefab, UUID(), "Sword", UUID::Generate()));
	}

	TEST_CASE("Moving an entity in the hierarchy undoes to its old place")
	{
		Fixture fixture;

		fixture.CheckUndoRedo(CreateScope<SetParentCommand>(fixture.Shield, UUID(), 0));
		CHECK(GetChildNames(fixture, UUID()) == std::vector<std::string>{"Shield", "Player", "Enemy"});

		fixture.CheckUndoRedo(CreateScope<SetParentCommand>(fixture.Enemy, fixture.Sword));
		CHECK(fixture.Get(fixture.Enemy).Get<HierarchyComponent>().Parent == fixture.Sword);
	}

	TEST_CASE("An entity can't move under itself or its descendants")
	{
		Fixture fixture;

		fixture.CheckFails(CreateScope<SetParentCommand>(fixture.Player, fixture.Player));
		fixture.CheckFails(CreateScope<SetParentCommand>(fixture.Player, fixture.Sword));
		fixture.CheckFails(CreateScope<SetParentCommand>(fixture.Player, UUID::Generate()));
		fixture.CheckFails(CreateScope<SetParentCommand>(UUID::Generate(), UUID()));
	}

	TEST_CASE("Adding a component with field values undoes to the entity without it")
	{
		Fixture fixture;

		fixture.CheckUndoRedo(CreateScope<AddComponentCommand>(fixture.Enemy, "Health", Json::Value{{"Points", 30}}));
		CHECK(fixture.Get(fixture.Enemy).Get<HealthComponent>().Points == 30);
		CHECK(fixture.History.GetUndoName() == "Add Health component");
	}

	TEST_CASE("Adding a component fails for components that exist, are unknown or are internal")
	{
		Fixture fixture;

		CHECK(fixture.CheckFails(CreateScope<AddComponentCommand>(fixture.Player, "Health")).GetCode() ==
			ErrorCode::AlreadyExists);
		CHECK(fixture.CheckFails(CreateScope<AddComponentCommand>(fixture.Enemy, "Mana")).GetCode() ==
			ErrorCode::NotFound);
		fixture.CheckFails(CreateScope<AddComponentCommand>(fixture.Enemy, "Brain"));
		fixture.CheckFails(CreateScope<AddComponentCommand>(fixture.Enemy, "Health", Json::Value{{"Points", 5000}}));
		fixture.CheckFails(CreateScope<AddComponentCommand>(fixture.Enemy, "Health", Json::Value{{"Armor", 1}}));
		fixture.CheckFails(CreateScope<AddComponentCommand>(UUID::Generate(), "Health"));
	}

	TEST_CASE("Removing a component undoes to the component with its values")
	{
		Fixture fixture;

		fixture.CheckUndoRedo(CreateScope<RemoveComponentCommand>(fixture.Player, "Health"));
		CHECK_FALSE(fixture.Get(fixture.Player).Has<HealthComponent>());

		REQUIRE(fixture.History.Undo(fixture.Scene).has_value());
		CHECK(fixture.Get(fixture.Player).Get<HealthComponent>().Points == 80);
		CHECK(fixture.Get(fixture.Player).Get<HealthComponent>().LastAttacker == fixture.Enemy);
	}

	TEST_CASE("Required components and components an entity lacks can't be removed")
	{
		Fixture fixture;

		fixture.CheckFails(CreateScope<RemoveComponentCommand>(fixture.Player, "Transform"));
		fixture.CheckFails(CreateScope<RemoveComponentCommand>(fixture.Player, "Name"));
		CHECK(fixture.CheckFails(CreateScope<RemoveComponentCommand>(fixture.Enemy, "Health")).GetCode() ==
			ErrorCode::NotFound);
	}

	TEST_CASE("Setting fields undoes to their old values")
	{
		Fixture fixture;

		fixture.CheckUndoRedo(CreateScope<SetFieldsCommand>(
			fixture.Player, "Transform", Json::Value{{"Position", {0.0, 0.0, 0.0}}, {"Scale", {2.0, 2.0, 2.0}}}));

		// A copy: the entity handle is a temporary
		const TransformComponent transform = fixture.Get(fixture.Player).GetTransform();
		CHECK(transform.Position == glm::vec3(0.0f));
		CHECK(transform.Scale == glm::vec3(2.0f));
		CHECK(fixture.History.GetUndoName() == "Set Transform Position, Scale");
	}

	TEST_CASE("Setting fields fails for read-only, unknown or invalid fields, changing nothing")
	{
		Fixture fixture;

		fixture.CheckFails(
			CreateScope<SetFieldsCommand>(fixture.Player, "ID", Json::Value{{"ID", UUID::Generate().ToString()}}));
		fixture.CheckFails(CreateScope<SetFieldsCommand>(
			fixture.Player, "Hierarchy", Json::Value{{"Parent", fixture.Enemy.ToString()}}));
		// The first field is valid, the second isn't: neither is set
		fixture.CheckFails(CreateScope<SetFieldsCommand>(
			fixture.Player, "Health", Json::Value{{"LastAttacker", UUID().ToString()}, {"Points", 2000}}));
		fixture.CheckFails(CreateScope<SetFieldsCommand>(fixture.Player, "Health", Json::Value{{"Armor", 1}}));
		fixture.CheckFails(CreateScope<SetFieldsCommand>(fixture.Enemy, "Health", Json::Value{{"Points", 1}}));
		fixture.CheckFails(CreateScope<SetFieldsCommand>(fixture.Player, "Health", Json::Value::array()));
		fixture.CheckFails(CreateScope<SetFieldsCommand>(fixture.Player, "Health", Json::Value::object()));
		fixture.CheckFails(CreateScope<SetFieldsCommand>(fixture.Player, "Brain", Json::Value::object()));
	}

	TEST_CASE("Continuous field edits merge into one undo step")
	{
		Fixture fixture;
		const glm::vec3 start = fixture.Get(fixture.Player).GetTransform().Position;
		for (int step = 1; step <= 5; ++step)
		{
			const double x = step;
			REQUIRE(fixture
					.Run(CreateScope<SetFieldsCommand>(
						fixture.Player, "Transform", Json::Value{{"Position", {x, 0.0, 0.0}}}, true))
					.has_value());
		}
		fixture.History.EndMerge();

		CHECK(fixture.History.GetUndoNames().size() == 1);
		CHECK(fixture.Get(fixture.Player).GetTransform().Position == glm::vec3(5.0f, 0.0f, 0.0f));
		REQUIRE(fixture.History.Undo(fixture.Scene).has_value());
		CHECK(fixture.Get(fixture.Player).GetTransform().Position == start);
		REQUIRE(fixture.History.Redo(fixture.Scene).has_value());
		CHECK(fixture.Get(fixture.Player).GetTransform().Position == glm::vec3(5.0f, 0.0f, 0.0f));
	}

	TEST_CASE("Only continuous edits of the same fields of the same component merge")
	{
		Fixture fixture;
		const auto set = [&fixture](UUID id, const char* type, Json::Value fields, bool continuous)
		{ REQUIRE(fixture.Run(CreateScope<SetFieldsCommand>(id, type, std::move(fields), continuous)).has_value()); };
		const Json::Value position = {{"Position", {1.0, 1.0, 1.0}}};

		set(fixture.Player, "Transform", position, true);
		set(fixture.Player, "Transform", {{"Scale", {3.0, 3.0, 3.0}}}, true);
		set(fixture.Enemy, "Transform", position, true);
		set(fixture.Player, "Health", {{"Points", 1}}, true);
		set(fixture.Player, "Health", {{"Points", 2}}, false);
		set(fixture.Player, "Health", {{"Points", 3}}, false);

		CHECK(fixture.History.GetUndoNames().size() == 6);
	}

	TEST_CASE("Command names describe what they change")
	{
		Fixture fixture;
		// Before they run, commands that learn names as they run name entities by UUID
		CHECK(DestroyEntityCommand(fixture.Enemy).GetName() == "Delete entity " + fixture.Enemy.ToString());
		CHECK(DuplicateEntityCommand(fixture.Enemy).GetName() == "Duplicate entity " + fixture.Enemy.ToString());

		REQUIRE(fixture.Run(CreateScope<DuplicateEntityCommand>(fixture.Enemy)).has_value());
		CHECK(fixture.History.GetUndoName() == "Duplicate entity 'Enemy'");
		REQUIRE(fixture.Run(CreateScope<SetParentCommand>(fixture.Enemy, fixture.Player)).has_value());
		CHECK(fixture.History.GetUndoName() == "Move entity");
		REQUIRE(fixture.Run(CreateScope<RemoveComponentCommand>(fixture.Player, "Health")).has_value());
		CHECK(fixture.History.GetUndoName() == "Remove Health component");
	}

}
