#include "Lodestone/Scene/PrefabSerializer.h"

#include "Common/DescribeError.h"
#include "Common/TemporaryDirectory.h"
#include "Lodestone/Core/FileSystem.h"
#include "Lodestone/Scene/Entity.h"

#include <doctest/doctest.h>

namespace Lodestone {

	namespace {

		constexpr UUID PrefabAsset{0x1234'5678'9ABC'4DEFull, 0x8123'4567'89AB'CDEFull};

		// A lamp post: a pole with a light on top
		Entity MakeLampPost(Scene& scene)
		{
			Entity pole = scene.CreateEntity("LampPost");
			Entity light = scene.CreateEntity("Light");
			REQUIRE(scene.SetParent(light, pole).has_value());
			pole.GetTransform().Scale = glm::vec3(0.2f, 3.0f, 0.2f);
			light.GetTransform().Position = glm::vec3(0.0f, 1.6f, 0.0f);
			return pole;
		}

	}

	TEST_CASE("A prefab document holds its root's tree")
	{
		Scene scene;
		const Entity root = MakeLampPost(scene);

		const Json::Value document = PrefabSerializer::Serialize(scene, root);

		CHECK(document["format"] == "Lodestone.Prefab");
		CHECK(document["version"] == 1);
		REQUIRE(document["entities"].size() == 2);
		CHECK(document["entities"][0]["components"]["Name"]["Name"] == "LampPost");
	}

	TEST_CASE("A prefab round-trips through text and instances as an independent copy")
	{
		Scene source;
		const Entity root = MakeLampPost(source);
		const auto entities = PrefabSerializer::DeserializeFromText(PrefabSerializer::SerializeToText(source, root));
		REQUIRE_MESSAGE(entities.has_value(), Testing::DescribeError(entities));

		Scene scene;
		const Entity street = scene.CreateEntity("Street");
		const auto first = PrefabSerializer::Instantiate(scene, *entities, PrefabAsset, street);
		const auto second = PrefabSerializer::Instantiate(scene, *entities, PrefabAsset);

		REQUIRE(first.has_value());
		REQUIRE(second.has_value());
		CHECK(scene.GetEntityCount() == 5);
		CHECK(first->GetId() != second->GetId());
		CHECK(first->GetId() != root.GetId());
		CHECK(first->Get<HierarchyComponent>().Parent == street.GetId());
		CHECK(second->Get<HierarchyComponent>().Parent.IsNil());
		CHECK(first->GetTransform().Scale == glm::vec3(0.2f, 3.0f, 0.2f));
		// The instance's root records the prefab; its children don't
		REQUIRE(first->Has<PrefabInstanceComponent>());
		CHECK(first->Get<PrefabInstanceComponent>().Prefab == PrefabAsset);
		const Entity light = scene.FindEntity(first->Get<HierarchyComponent>().Children.at(0));
		CHECK_FALSE(light.Has<PrefabInstanceComponent>());
	}

	TEST_CASE("A prefab that isn't an asset instances without a PrefabInstance component")
	{
		Scene source;
		const Entity root = MakeLampPost(source);
		const Json::Value entities = PrefabSerializer::Serialize(source, root)["entities"];
		Scene scene;

		const auto instance = PrefabSerializer::Instantiate(scene, entities, UUID());

		REQUIRE(instance.has_value());
		CHECK_FALSE(instance->Has<PrefabInstanceComponent>());
	}

	TEST_CASE("A prefab saved to a file loads back")
	{
		const Testing::TemporaryDirectory directory("PrefabFile");
		Scene source;
		const Entity root = MakeLampPost(source);
		const std::filesystem::path path = directory.GetPath() / "LampPost.lprefab";

		REQUIRE(PrefabSerializer::Save(source, root, path).has_value());
		const auto entities = PrefabSerializer::Load(path);

		REQUIRE_MESSAGE(entities.has_value(), Testing::DescribeError(entities));
		CHECK(*entities == PrefabSerializer::Serialize(source, root)["entities"]);
	}

	TEST_CASE("Version 1 prefab files load")
	{
		const auto entities =
			PrefabSerializer::Load(std::filesystem::path(LS_TEST_FIXTURE_DIR) / "Prefabs" / "LampPost.v1.lprefab");
		REQUIRE_MESSAGE(entities.has_value(), Testing::DescribeError(entities));
		Scene scene;

		const auto instance = PrefabSerializer::Instantiate(scene, *entities, PrefabAsset);

		REQUIRE(instance.has_value());
		CHECK(instance->GetName() == "LampPost");
		CHECK(instance->GetTransform().Scale == glm::vec3(0.2f, 3.0f, 0.2f));
		const auto& children = instance->Get<HierarchyComponent>().Children;
		REQUIRE(children.size() == 1);
		const Entity light = scene.FindEntity(children[0]);
		CHECK(light.GetName() == "Light");
		CHECK(light.GetTransform().Position == glm::vec3(0.0f, 0.55f, 0.0f));
		// Fields the file leaves out get their defaults
		CHECK(light.GetTransform().Scale == glm::vec3(1.0f));
	}

	TEST_CASE("Invalid prefabs fail to load")
	{
		Scene source;
		const Entity root = MakeLampPost(source);
		const Json::Value valid = PrefabSerializer::Serialize(source, root);

		SUBCASE("Wrong format")
		{
			Json::Value document = valid;
			document["format"] = "Lodestone.Scene";
			CHECK_FALSE(PrefabSerializer::Deserialize(document).has_value());
		}
		SUBCASE("Unknown member")
		{
			Json::Value document = valid;
			document["extra"] = 1;
			CHECK_FALSE(PrefabSerializer::Deserialize(document).has_value());
		}
		SUBCASE("No entities")
		{
			Json::Value document = valid;
			document["entities"] = Json::Value::array();
			CHECK_FALSE(PrefabSerializer::Deserialize(document).has_value());
		}
		SUBCASE("Two roots")
		{
			Json::Value document = valid;
			document["entities"][1]["components"]["Hierarchy"]["Parent"] = UUID().ToString();
			CHECK_FALSE(PrefabSerializer::Deserialize(document).has_value());
		}
		SUBCASE("A field of the wrong type")
		{
			Json::Value document = valid;
			document["entities"][0]["components"]["Name"]["Name"] = 5;
			CHECK_FALSE(PrefabSerializer::Deserialize(document).has_value());
		}
		SUBCASE("Not JSON")
		{
			CHECK_FALSE(PrefabSerializer::DeserializeFromText("{").has_value());
		}
		SUBCASE("A missing file")
		{
			const Testing::TemporaryDirectory directory("PrefabMissing");
			const auto loaded = PrefabSerializer::Load(directory.GetPath() / "Missing.lprefab");
			REQUIRE_FALSE(loaded.has_value());
			CHECK(loaded.error().GetCode() == ErrorCode::FileNotFound);
		}
	}

}
