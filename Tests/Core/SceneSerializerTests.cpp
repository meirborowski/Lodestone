#include "Lodestone/Scene/SceneSerializer.h"

#include "Common/DescribeError.h"
#include "Common/TemporaryDirectory.h"
#include "Lodestone/Core/FileSystem.h"
#include "Lodestone/Scene/Entity.h"
#include "Lodestone/Serialization/Json.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

namespace Lodestone {

	namespace {

		struct ScoreComponent
		{
			int32_t Points = 0;
			std::string Owner;
		};

		struct SpinComponent
		{
			float Speed = 1.0f;
		};

		// The core components plus two test components, one of them internal
		const ComponentRegistry& GetTestComponents()
		{
			static const ComponentRegistry* s_Registry = []
			{
				auto* registry = new ComponentRegistry();
				RegisterCoreComponents(*registry);
				registry->Register<ScoreComponent>("Score")
					.Field("Points", &ScoreComponent::Points, {.Min = 0.0})
					.Field("Owner", &ScoreComponent::Owner);
				registry->Register<SpinComponent>("Spin", {}, ComponentFlags::Internal)
					.Field("Speed", &SpinComponent::Speed);
				return registry;
			}();
			return *s_Registry;
		}

		// A scene with a hierarchy, transforms and an optional component
		Scope<Scene> MakeScene()
		{
			auto scene = CreateScope<Scene>(GetTestComponents());
			const Entity world = scene->CreateEntity("World");
			Entity player = scene->CreateEntity("Player");
			Entity weapon = scene->CreateEntity("Weapon");
			const Entity hat = scene->CreateEntity("Hat");
			REQUIRE(scene->SetParent(player, world).has_value());
			REQUIRE(scene->SetParent(hat, player).has_value());
			REQUIRE(scene->SetParent(weapon, player, 0).has_value());
			player.GetTransform().Position = glm::vec3(1.0f, 2.0f, 3.0f);
			player.GetTransform().Rotation = glm::normalize(glm::quat::wxyz(0.9f, 0.1f, 0.2f, 0.3f));
			weapon.GetTransform().Scale = glm::vec3(0.5f);
			player.Add<ScoreComponent>(ScoreComponent{.Points = 12, .Owner = "Ana"});
			player.Add<SpinComponent>();
			scene->CreateEntity("Sun");
			return scene;
		}

		std::string SerializeToText(const Scene& scene)
		{
			return SceneSerializer::SerializeToText(scene);
		}

		std::expected<Scope<Scene>, Error> Load(const Json::Value& document)
		{
			return SceneSerializer::Deserialize(document, GetTestComponents());
		}

		// A minimal valid document with one entity, for tests that break one thing in it
		Json::Value MakeDocument()
		{
			return Json::Parse(R"({"format": "Lodestone.Scene", "version": 1, "entities": [
				{"components": {"ID": {"ID": "11111111-1111-4111-8111-111111111111"}}}]})")
				.value();
		}

		void ExpectLoadError(const Json::Value& document, std::string_view messagePart)
		{
			CAPTURE(document.dump());
			const auto scene = Load(document);
			REQUIRE_FALSE(scene.has_value());
			CAPTURE(scene.error().GetMessageText());
			CHECK(scene.error().GetMessageText().contains(messagePart));
		}

	}

	TEST_CASE("A scene round-trips through its file format unchanged")
	{
		const auto original = MakeScene();
		const std::string text = SerializeToText(*original);

		const auto loaded = SceneSerializer::DeserializeFromText(text, GetTestComponents());
		REQUIRE_MESSAGE(loaded.has_value(), Testing::DescribeError(loaded));

		// Saving the loaded scene gives the same text: entities, components, values and hierarchy order all survived
		CHECK(SerializeToText(**loaded) == text);
		CHECK((*loaded)->GetEntityCount() == original->GetEntityCount());
	}

	TEST_CASE("Loaded scenes keep entity UUIDs, components and the hierarchy")
	{
		const auto original = MakeScene();
		const auto loaded = SceneSerializer::DeserializeFromText(SerializeToText(*original), GetTestComponents());
		REQUIRE(loaded.has_value());
		Scene& scene = **loaded;

		for (const auto [handle, id] : original->GetRegistry().view<const IDComponent>().each())
		{
			Entity before(handle, original.get());
			Entity after = scene.FindEntity(id.ID);
			REQUIRE(after.IsValid());
			CHECK(after.GetName() == before.GetName());
			CHECK(after.GetTransform().Position == before.GetTransform().Position);
			CHECK(after.GetTransform().Rotation == before.GetTransform().Rotation);
			CHECK(after.GetTransform().Scale == before.GetTransform().Scale);
			CHECK(after.Get<HierarchyComponent>().Parent == before.Get<HierarchyComponent>().Parent);
			CHECK(after.Get<HierarchyComponent>().Children == before.Get<HierarchyComponent>().Children);
			// Internal components aren't saved
			CHECK_FALSE(after.Has<SpinComponent>());
		}
		CHECK(std::ranges::equal(scene.GetRootEntities(), original->GetRootEntities()));

		Entity player = scene.FindEntity(original->GetRootEntities()[0]);
		player = scene.FindEntity(player.Get<HierarchyComponent>().Children[0]);
		REQUIRE(player.Has<ScoreComponent>());
		CHECK(player.Get<ScoreComponent>().Points == 12);
		CHECK(player.Get<ScoreComponent>().Owner == "Ana");
	}

	TEST_CASE("Saved scenes start with the format and version, and list parents before their children")
	{
		const auto scene = MakeScene();
		const Json::Value document = SceneSerializer::Serialize(*scene);

		REQUIRE(document.size() == 3);
		CHECK(document.begin().key() == "format");
		CHECK(document["format"] == "Lodestone.Scene");
		CHECK(document["version"] == 1);
		// Components in the order they were registered
		CHECK(document["entities"][0]["components"].begin().key() == "ID");
		std::vector<std::string> names;
		for (const Json::Value& entity : document["entities"])
			names.push_back(entity["components"]["Name"]["Name"].get<std::string>());
		CHECK(names == std::vector<std::string>{"World", "Player", "Weapon", "Hat", "Sun"});
	}

	TEST_CASE("Scenes are saved to and loaded from files")
	{
		const Testing::TemporaryDirectory directory("SceneFiles");
		const std::filesystem::path path = directory.GetPath() / "Level.lscene";
		const auto original = MakeScene();

		REQUIRE(SceneSerializer::Save(*original, path).has_value());
		const auto loaded = SceneSerializer::Load(path, GetTestComponents());
		REQUIRE_MESSAGE(loaded.has_value(), Testing::DescribeError(loaded));
		CHECK(SerializeToText(**loaded) == SerializeToText(*original));

		const auto missing = SceneSerializer::Load(directory.GetPath() / "Missing.lscene");
		REQUIRE_FALSE(missing.has_value());
		CHECK(missing.error().GetCode() == ErrorCode::FileNotFound);
	}

	TEST_CASE("Version 1 scene files load")
	{
		const auto loaded =
			SceneSerializer::Load(std::filesystem::path(LS_TEST_FIXTURE_DIR) / "Scenes" / "Hierarchy.v1.lscene");
		REQUIRE_MESSAGE(loaded.has_value(), Testing::DescribeError(loaded));
		Scene& scene = **loaded;

		CHECK(scene.GetEntityCount() == 3);
		Entity player = scene.FindEntity(UUID(0x22222222'2222'4222ull, 0x8222'222222222222ull));
		Entity camera = scene.FindEntity(UUID(0x33333333'3333'4333ull, 0x8333'333333333333ull));
		REQUIRE(player.IsValid());
		REQUIRE(camera.IsValid());
		CHECK(player.GetName() == "Player");
		// Fields missing from the file keep their defaults
		CHECK(player.GetTransform().Position == glm::vec3(1.5f, 0.0f, -2.25f));
		CHECK(player.GetTransform().Scale == glm::vec3(1.0f));
		CHECK(camera.Get<HierarchyComponent>().Parent == player.GetId());
		CHECK(player.Get<HierarchyComponent>().Children == std::vector<UUID>{camera.GetId()});
		CHECK(camera.GetTransform().Rotation.y == doctest::Approx(0.7071068f));
		CHECK(scene.GetRootEntities().size() == 2);
	}

	TEST_CASE("Loading rejects invalid scenes with a message saying what's wrong")
	{
		SUBCASE("An unknown component")
		{
			Json::Value document = MakeDocument();
			document["entities"][0]["components"]["Missing"] = Json::Value::object();
			ExpectLoadError(document, "there's no Missing component");
		}
		SUBCASE("An internal component")
		{
			Json::Value document = MakeDocument();
			document["entities"][0]["components"]["Spin"] = {{"Speed", 2.0}};
			ExpectLoadError(document, "internal component");
		}
		SUBCASE("An unknown field")
		{
			Json::Value document = MakeDocument();
			document["entities"][0]["components"]["Transform"] = {{"Positon", {1, 2, 3}}};
			ExpectLoadError(document, "Transform has no field 'Positon'");
		}
		SUBCASE("A value of the wrong type")
		{
			Json::Value document = MakeDocument();
			document["entities"][0]["components"]["Name"] = {{"Name", 5}};
			ExpectLoadError(document, "entities[0].components.Name.Name");
		}
		SUBCASE("A value out of range")
		{
			Json::Value document = MakeDocument();
			document["entities"][0]["components"]["Score"] = {{"Points", -1}};
			ExpectLoadError(document, "Points");
		}
		SUBCASE("An entity without a UUID")
		{
			Json::Value document = MakeDocument();
			document["entities"].push_back({{"components", {{"Name", {{"Name", "Anonymous"}}}}}});
			ExpectLoadError(document, "entities[1].components: 'ID' is missing");
		}
		SUBCASE("Two entities with the same UUID")
		{
			Json::Value document = MakeDocument();
			document["entities"].push_back(document["entities"][0]);
			ExpectLoadError(document, "already has an entity");
		}
		SUBCASE("The nil UUID")
		{
			Json::Value document = MakeDocument();
			document["entities"][0]["components"]["ID"]["ID"] = UUID().ToString();
			ExpectLoadError(document, "nil UUID");
		}
		SUBCASE("A parent that doesn't exist")
		{
			Json::Value document = MakeDocument();
			document["entities"][0]["components"]["Hierarchy"] = {{"Parent", UUID(5, 5).ToString()}};
			ExpectLoadError(document, "the scene has no entity");
		}
		SUBCASE("A cycle in the hierarchy")
		{
			Json::Value document = MakeDocument();
			Json::Value second = document["entities"][0];
			second["components"]["ID"]["ID"] = UUID(2, 2).ToString();
			second["components"]["Hierarchy"] = {{"Parent", "11111111-1111-4111-8111-111111111111"}};
			document["entities"][0]["components"]["Hierarchy"] = {{"Parent", UUID(2, 2).ToString()}};
			document["entities"].push_back(second);
			ExpectLoadError(document, "the hierarchy has a cycle");
		}
		SUBCASE("An unexpected member")
		{
			Json::Value document = MakeDocument();
			document["entites"] = Json::Value::array();
			ExpectLoadError(document, "unexpected member 'entites'");
		}
		SUBCASE("Entities that aren't an array")
		{
			Json::Value document = MakeDocument();
			document["entities"] = Json::Value::object();
			ExpectLoadError(document, "expected an array");
		}
		SUBCASE("Another file format")
		{
			Json::Value document = MakeDocument();
			document["format"] = "Lodestone.AssetMetadata";
			ExpectLoadError(document, "not a Lodestone.Scene file");
		}
		SUBCASE("A newer version")
		{
			Json::Value document = MakeDocument();
			document["version"] = 2;
			const auto scene = Load(document);
			REQUIRE_FALSE(scene.has_value());
			CHECK(scene.error().GetCode() == ErrorCode::UnsupportedVersion);
		}
	}

	TEST_CASE("Text that isn't a scene fails to load")
	{
		for (const char* text : {"", "not json", "[]", "{}", R"({"format": "Lodestone.Scene"})"})
		{
			CAPTURE(text);
			CHECK_FALSE(SceneSerializer::DeserializeFromText(text, GetTestComponents()).has_value());
		}
	}

}
