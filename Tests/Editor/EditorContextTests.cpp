#include "Lodestone/Editor/EditorContext.h"

#include "Common/DescribeError.h"
#include "Common/TemporaryDirectory.h"
#include "Lodestone/Core/FileSystem.h"
#include "Lodestone/Editor/Commands/EntityCommands.h"
#include "Lodestone/Scene/Entity.h"
#include "Lodestone/Scene/EntitySerializer.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Lodestone {

	namespace {

		// An editor with a new project open
		struct Fixture
		{
			Fixture()
			{
				const auto created = Context.CreateProject(Directory.GetPath() / "Game", "Game");
				REQUIRE_MESSAGE(created.has_value(), Testing::DescribeError(created));
			}

			UUID CreateEntity(std::string name, UUID parent = {}, Json::Value components = Json::Value::object())
			{
				auto command =
					CreateScope<CreateEntityCommand>(std::move(name), parent, std::nullopt, std::move(components));
				const CreateEntityCommand& created = *command;
				const auto executed = Context.Execute(std::move(command));
				REQUIRE_MESSAGE(executed.has_value(), Testing::DescribeError(executed));
				return created.GetEntityId();
			}

			std::filesystem::path GetAssetDirectory() const { return Context.GetProject()->GetAssetDirectory(); }

			Testing::TemporaryDirectory Directory{"EditorContext"};
			EditorContext Context;
		};

	}

	TEST_CASE("The editor starts with an empty, unsaved scene and no project")
	{
		const EditorContext context;

		CHECK_FALSE(context.HasProject());
		CHECK(context.GetAssets() == nullptr);
		CHECK(context.GetScene().GetEntityCount() == 0);
		CHECK(context.GetDocumentKind() == EditorContext::DocumentKind::Scene);
		CHECK_FALSE(context.GetDocumentPath().has_value());
		CHECK_FALSE(context.HasUnsavedChanges());
		CHECK(context.GetPlayState() == PlayState::Editing);
		CHECK(context.GetSelection().IsNil());
	}

	TEST_CASE("Creating and opening projects starts a new scene")
	{
		Fixture fixture;
		CHECK(fixture.Context.HasProject());
		CHECK(fixture.Context.GetProject()->GetSettings().Name == "Game");
		REQUIRE(fixture.Context.GetAssets() != nullptr);
		fixture.CreateEntity("Leftover");

		const auto opened = fixture.Context.OpenProject(fixture.Directory.GetPath() / "Game");

		REQUIRE(opened.has_value());
		CHECK(fixture.Context.GetScene().GetEntityCount() == 0);
		CHECK_FALSE(fixture.Context.HasUnsavedChanges());
		CHECK_FALSE(fixture.Context.GetHistory().CanUndo());
	}

	TEST_CASE("A failed project change keeps the open project")
	{
		Fixture fixture;

		CHECK_FALSE(fixture.Context.OpenProject(fixture.Directory.GetPath() / "Nowhere").has_value());
		CHECK_FALSE(fixture.Context.CreateProject(fixture.Directory.GetPath() / "Game", "Again").has_value());

		CHECK(fixture.Context.GetProject()->GetSettings().Name == "Game");
	}

	TEST_CASE("Asset paths are resolved inside the asset directory and can't leave it")
	{
		Fixture fixture;
		const std::filesystem::path assets = fixture.GetAssetDirectory().lexically_normal();

		const auto inside = fixture.Context.ResolveAssetPath("Scenes/../Scenes/Main.lscene");
		REQUIRE(inside.has_value());
		CHECK(*inside == assets / "Scenes" / "Main.lscene");

		const auto checkOutside = [&fixture](std::initializer_list<std::string_view> paths)
		{
			for (const std::string_view path : paths)
			{
				CAPTURE(path);
				CHECK_FALSE(fixture.Context.ResolveAssetPath(path).has_value());
			}
		};
		checkOutside({"", ".", "..", "../Project.lsproject", "Scenes/../../Secret.txt", "/etc/passwd"});
#if LS_PLATFORM_WINDOWS
		// Elsewhere these are ordinary relative paths
		checkOutside({"C:/Windows/win.ini", "C:Secret.txt", R"(\\server\share\file)", R"(\Secret.txt)"});
#endif
	}

	TEST_CASE("Asset paths can't be resolved without a project")
	{
		const EditorContext context;
		const auto resolved = context.ResolveAssetPath("Main.lscene");
		REQUIRE_FALSE(resolved.has_value());
		CHECK(resolved.error().GetCode() == ErrorCode::InvalidState);
	}

	TEST_CASE("A saved scene reopens, and saving clears the unsaved changes")
	{
		Fixture fixture;
		const UUID parent = fixture.CreateEntity("Parent");
		fixture.CreateEntity("Child", parent, {{"Transform", {{"Position", {1.0, 2.0, 3.0}}}}});
		CHECK(fixture.Context.HasUnsavedChanges());

		CHECK_FALSE(fixture.Context.SaveDocument().has_value());
		const auto saved = fixture.Context.SaveDocument("Levels/One.lscene");
		REQUIRE_MESSAGE(saved.has_value(), Testing::DescribeError(saved));
		CHECK_FALSE(fixture.Context.HasUnsavedChanges());
		CHECK(fixture.Context.GetDocumentPath() == "Levels/One.lscene");
		CHECK(std::filesystem::is_regular_file(fixture.GetAssetDirectory() / "Levels" / "One.lscene"));
		// Saving registers the asset
		CHECK(fixture.Context.GetAssets()->FindByPath("Levels/One.lscene") != nullptr);
		const Json::Value before = EntitySerializer::SerializeScene(fixture.Context.GetScene());

		REQUIRE(fixture.Context.NewScene().has_value());
		CHECK(fixture.Context.GetScene().GetEntityCount() == 0);
		const auto opened = fixture.Context.OpenScene("Levels/One.lscene");

		REQUIRE_MESSAGE(opened.has_value(), Testing::DescribeError(opened));
		CHECK(EntitySerializer::SerializeScene(fixture.Context.GetScene()) == before);
		CHECK(fixture.Context.GetDocumentPath() == "Levels/One.lscene");
		CHECK_FALSE(fixture.Context.HasUnsavedChanges());
	}

	TEST_CASE("Undoing back to the saved state means there are no unsaved changes")
	{
		Fixture fixture;
		fixture.CreateEntity("A");
		REQUIRE(fixture.Context.SaveDocument("Main.lscene").has_value());

		fixture.CreateEntity("B");
		CHECK(fixture.Context.HasUnsavedChanges());
		REQUIRE(fixture.Context.Undo().has_value());
		CHECK_FALSE(fixture.Context.HasUnsavedChanges());
		REQUIRE(fixture.Context.Undo().has_value());
		CHECK(fixture.Context.HasUnsavedChanges());
		REQUIRE(fixture.Context.Redo().has_value());
		CHECK_FALSE(fixture.Context.HasUnsavedChanges());
	}

	TEST_CASE("Documents are saved and opened only with their own extension")
	{
		Fixture fixture;
		fixture.CreateEntity("A");

		CHECK_FALSE(fixture.Context.SaveDocument("Main.lprefab").has_value());
		CHECK_FALSE(fixture.Context.SaveDocument("Main.json").has_value());
		CHECK_FALSE(fixture.Context.SaveDocument("../Main.lscene").has_value());
		CHECK_FALSE(fixture.Context.OpenScene("Missing.lscene").has_value());
		CHECK(fixture.Context.GetScene().GetEntityCount() == 1);
	}

	TEST_CASE("A saved prefab can be opened as a document, edited and saved again")
	{
		Fixture fixture;
		const UUID lamp = fixture.CreateEntity("Lamp");
		fixture.CreateEntity("Bulb", lamp);
		CHECK_FALSE(fixture.Context.SavePrefab(lamp, "Prefabs/Lamp.lscene").has_value());
		const auto saved = fixture.Context.SavePrefab(lamp, "Prefabs/Lamp.lprefab");
		REQUIRE_MESSAGE(saved.has_value(), Testing::DescribeError(saved));
		CHECK(fixture.Context.GetAssets()->FindByPath("Prefabs/Lamp.lprefab") != nullptr);
		REQUIRE(fixture.Context.SaveDocument("Main.lscene").has_value());

		const auto opened = fixture.Context.OpenPrefab("Prefabs/Lamp.lprefab");

		REQUIRE_MESSAGE(opened.has_value(), Testing::DescribeError(opened));
		CHECK(fixture.Context.GetDocumentKind() == EditorContext::DocumentKind::Prefab);
		CHECK(fixture.Context.GetDocumentPath() == "Prefabs/Lamp.lprefab");
		REQUIRE(fixture.Context.GetScene().GetRootEntities().size() == 1);
		const UUID root = fixture.Context.GetScene().GetRootEntities()[0];
		CHECK(root == lamp);

		// Editing it as a document: a prefab keeps exactly one root
		fixture.CreateEntity("Shade", root);
		REQUIRE(fixture.Context.SaveDocument().has_value());
		const UUID extra = fixture.CreateEntity("Stray");
		CHECK_FALSE(fixture.Context.SaveDocument().has_value());
		REQUIRE(fixture.Context.Execute(CreateScope<DestroyEntityCommand>(extra)).has_value());

		// Instances made afterwards have the change
		REQUIRE(fixture.Context.OpenScene("Main.lscene").has_value());
		const auto instance = fixture.Context.InstantiatePrefab("Prefabs/Lamp.lprefab");
		REQUIRE_MESSAGE(instance.has_value(), Testing::DescribeError(instance));
		const Entity instanceRoot = fixture.Context.GetScene().FindEntity(*instance);
		CHECK(instanceRoot.Get<HierarchyComponent>().Children.size() == 2);
	}

	TEST_CASE("Instancing a prefab is a command, and records the prefab asset")
	{
		Fixture fixture;
		const UUID crate = fixture.CreateEntity("Crate");
		REQUIRE(fixture.Context.SavePrefab(crate, "Crate.lprefab").has_value());
		const UUID parent = fixture.CreateEntity("Stack");

		const auto instance = fixture.Context.InstantiatePrefab("Crate.lprefab", parent);

		REQUIRE_MESSAGE(instance.has_value(), Testing::DescribeError(instance));
		const Entity entity = fixture.Context.GetScene().FindEntity(*instance);
		REQUIRE(entity);
		CHECK(entity.GetName() == "Crate");
		CHECK(entity.Get<HierarchyComponent>().Parent == parent);
		CHECK(entity.Get<PrefabInstanceComponent>().Prefab ==
			fixture.Context.GetAssets()->FindByPath("Crate.lprefab")->Metadata.Id);
		CHECK(fixture.Context.GetHistory().GetUndoName() == "Instance prefab 'Crate'");
		REQUIRE(fixture.Context.Undo().has_value());
		CHECK_FALSE(fixture.Context.GetScene().Contains(*instance));

		CHECK_FALSE(fixture.Context.InstantiatePrefab("Missing.lprefab").has_value());
		CHECK_FALSE(fixture.Context.InstantiatePrefab("Crate.lscene").has_value());
		CHECK_FALSE(fixture.Context.InstantiatePrefab("Crate.lprefab", UUID::Generate()).has_value());
	}

	TEST_CASE("The selection is cleared when its entity is gone or the document changes")
	{
		Fixture fixture;
		const UUID entity = fixture.CreateEntity("Selected");
		fixture.Context.Select(entity);
		CHECK(fixture.Context.GetSelection() == entity);

		REQUIRE(fixture.Context.Undo().has_value());
		CHECK(fixture.Context.GetSelection().IsNil());
		REQUIRE(fixture.Context.Redo().has_value());
		// Redo brings back the same UUID, so the entity is selected again
		CHECK(fixture.Context.GetSelection() == entity);

		REQUIRE(fixture.Context.NewScene().has_value());
		CHECK(fixture.Context.GetSelection().IsNil());
	}

	TEST_CASE("Stopping play mode restores the scene exactly as it was")
	{
		Fixture fixture;
		const UUID player = fixture.CreateEntity("Player", {}, {{"Transform", {{"Position", {1.0, 0.0, 0.0}}}}});
		const UUID child = fixture.CreateEntity("Child", player);
		Scene& scene = fixture.Context.GetScene();
		const entt::entity playerHandle = scene.FindHandle(player);
		const Json::Value before = EntitySerializer::SerializeScene(scene);
		const uint64_t historyState = fixture.Context.GetHistory().GetStateId();

		REQUIRE(fixture.Context.StartPlay().has_value());
		CHECK(fixture.Context.GetPlayState() == PlayState::Playing);
		REQUIRE(fixture.Context.GetSimulation() != nullptr);
		// Gameplay changes the scene however it likes
		scene.FindEntity(player).GetTransform().Position = glm::vec3(50.0f);
		scene.DestroyEntity(scene.FindEntity(child));
		scene.CreateEntity("Spawned");
		REQUIRE(fixture.Context.Step(10).has_value());
		CHECK(fixture.Context.GetSimulation()->GetTick() == 10);

		REQUIRE(fixture.Context.StopPlay().has_value());

		CHECK(fixture.Context.GetPlayState() == PlayState::Editing);
		CHECK(fixture.Context.GetSimulation() == nullptr);
		CHECK(EntitySerializer::SerializeScene(fixture.Context.GetScene()) == before);
		CHECK(fixture.Context.GetScene().FindHandle(player) == playerHandle);
		CHECK(fixture.Context.GetHistory().GetStateId() == historyState);
		CHECK(fixture.Context.HasUnsavedChanges());
	}

	TEST_CASE("Edits, undo, redo and document changes are refused while playing")
	{
		Fixture fixture;
		const UUID entity = fixture.CreateEntity("A");
		REQUIRE(fixture.Context.StartPlay().has_value());

		CHECK_FALSE(fixture.Context.Execute(CreateScope<DestroyEntityCommand>(entity)).has_value());
		CHECK_FALSE(fixture.Context.Undo().has_value());
		CHECK_FALSE(fixture.Context.Redo().has_value());
		CHECK_FALSE(fixture.Context.NewScene().has_value());
		CHECK_FALSE(fixture.Context.SaveDocument("Main.lscene").has_value());
		CHECK_FALSE(fixture.Context.SavePrefab(entity, "A.lprefab").has_value());
		CHECK_FALSE(fixture.Context.OpenProject(fixture.Directory.GetPath() / "Game").has_value());
		CHECK_FALSE(fixture.Context.InstantiatePrefab("A.lprefab").has_value());
		CHECK_FALSE(fixture.Context.StartPlay().has_value());
		CHECK(fixture.Context.GetScene().Contains(entity));
	}

	TEST_CASE("Play mode pauses, resumes and steps")
	{
		Fixture fixture;
		CHECK_FALSE(fixture.Context.SetPaused(true).has_value());
		CHECK_FALSE(fixture.Context.Step(1).has_value());
		CHECK_FALSE(fixture.Context.StopPlay().has_value());
		REQUIRE(fixture.Context.StartPlay().has_value());

		REQUIRE(fixture.Context.SetPaused(true).has_value());
		CHECK(fixture.Context.GetPlayState() == PlayState::Paused);
		// Real time doesn't advance a paused simulation, but steps do
		fixture.Context.Update(1.0);
		CHECK(fixture.Context.GetSimulation()->GetTick() == 0);
		REQUIRE(fixture.Context.Step(3).has_value());
		CHECK(fixture.Context.GetSimulation()->GetTick() == 3);

		REQUIRE(fixture.Context.SetPaused(false).has_value());
		CHECK(fixture.Context.GetPlayState() == PlayState::Playing);
		// The project's tick rate is 60
		fixture.Context.Update(0.1);
		CHECK(fixture.Context.GetSimulation()->GetTick() == 9);
	}

	TEST_CASE("Queued input reaches play mode's systems a command per tick, before live input")
	{
		Fixture fixture;
		std::vector<InputCommand> received;
		fixture.Context.AddPlaySystem(
			"Record", [&received](SimulationContext& context) { received.push_back(context.GetInput(0)); });
		REQUIRE(fixture.Context.StartPlay().has_value());
		InputCommand jump;
		jump.KeysDown.set(std::to_underlying(Key::Space));
		InputCommand run;
		run.KeysDown.set(std::to_underlying(Key::LeftShift));
		const std::vector<InputCommand> queued = {jump, run};
		fixture.Context.QueueInput(queued);
		CHECK(fixture.Context.GetQueuedInputCount() == 2);

		REQUIRE(fixture.Context.Step(1).has_value());
		InputCommand live;
		live.KeysDown.set(std::to_underlying(Key::W));
		fixture.Context.Update(2.0 / 60.0 + 1e-6,
			[&live](uint64_t tick)
			{
				InputCommand command = live;
				command.Tick = tick;
				return std::vector<InputCommand>{command};
			});

		REQUIRE(received.size() == 3);
		CHECK(received[0].KeysDown.test(std::to_underlying(Key::Space)));
		CHECK(received[0].Tick == 0);
		CHECK(received[1].KeysDown.test(std::to_underlying(Key::LeftShift)));
		CHECK(received[1].Tick == 1);
		CHECK(received[2].KeysDown.test(std::to_underlying(Key::W)));
		CHECK(fixture.Context.GetQueuedInputCount() == 0);

		// Stopping drops input that's still queued
		fixture.Context.QueueInput(queued);
		REQUIRE(fixture.Context.StopPlay().has_value());
		CHECK(fixture.Context.GetQueuedInputCount() == 0);
	}

	TEST_CASE("Play mode uses the project's tick rate")
	{
		Fixture fixture;
		REQUIRE(fixture.Context.StartPlay().has_value());
		CHECK(fixture.Context.GetSimulation()->GetConfig().TickRate == 60);
		REQUIRE(fixture.Context.StopPlay().has_value());

		// Without a project, the default rate
		EditorContext context;
		REQUIRE(context.StartPlay().has_value());
		CHECK(context.GetSimulation()->GetConfig().TickRate == 60);
	}

	TEST_CASE("The editor's log keeps messages for the console and MCP")
	{
		EditorContext context;
		const uint64_t before = context.GetLog().GetLatestSequence();

		LS_CORE_WARN("A message for the editor log");

		const auto entries = context.GetLog().GetEntries(before);
		REQUIRE_FALSE(entries.empty());
		CHECK(entries.back().Message == "A message for the editor log");
		CHECK(entries.back().Level == LogLevel::Warn);
	}

}
