#include "Lodestone/Scene/EntitySerializer.h"

#include "Common/DescribeError.h"
#include "Lodestone/Scene/Entity.h"

#include <doctest/doctest.h>

#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Lodestone {

	namespace {

		// Refers to another entity by UUID, as gameplay components will
		struct FollowComponent
		{
			UUID Target;
			float Distance = 1.0f;
		};

		struct CacheComponent
		{
			int32_t Hits = 0;
		};

		const ComponentRegistry& GetTestComponents()
		{
			static const ComponentRegistry* s_Registry = []
			{
				auto* registry = new ComponentRegistry();
				RegisterCoreComponents(*registry);
				registry->Register<FollowComponent>("Follow")
					.Field("Target", &FollowComponent::Target)
					.Field("Distance", &FollowComponent::Distance, {.Min = 0.0});
				registry->Register<CacheComponent>("Cache", {}, ComponentFlags::Internal)
					.Field("Hits", &CacheComponent::Hits);
				return registry;
			}();
			return *s_Registry;
		}

		// Car (with Wheel and Driver, who follows Wheel) and Road
		struct TestScene
		{
			Scope<Scene> World;
			Entity Car;
			Entity Wheel;
			Entity Driver;
			Entity Road;
		};

		TestScene MakeScene()
		{
			TestScene test{.World = CreateScope<Scene>(GetTestComponents())};
			Scene& scene = *test.World;
			test.Car = scene.CreateEntity("Car");
			test.Wheel = scene.CreateEntity("Wheel");
			test.Driver = scene.CreateEntity("Driver");
			test.Road = scene.CreateEntity("Road");
			REQUIRE(scene.SetParent(test.Wheel, test.Car).has_value());
			REQUIRE(scene.SetParent(test.Driver, test.Car).has_value());
			test.Car.GetTransform().Position = glm::vec3(5.0f, 0.0f, 0.0f);
			test.Wheel.GetTransform().Position = glm::vec3(1.0f, 0.0f, 0.0f);
			test.Driver.Add<FollowComponent>(FollowComponent{.Target = test.Wheel.GetId(), .Distance = 2.0f});
			test.Driver.Add<CacheComponent>(CacheComponent{.Hits = 3});
			// A reference to an entity outside the tree
			test.Wheel.Add<FollowComponent>(FollowComponent{.Target = test.Road.GetId()});
			return test;
		}

		std::vector<std::string> GetNames(const Scene& scene, std::span<const UUID> ids)
		{
			std::vector<std::string> names;
			for (const UUID id : ids)
				names.push_back(scene.GetRegistry().get<NameComponent>(scene.FindHandle(id)).Name);
			return names;
		}

	}

	TEST_CASE("A serialized tree lists the root first, as a root entity, without internal components")
	{
		const TestScene test = MakeScene();

		const Json::Value tree = EntitySerializer::SerializeTree(*test.World, test.Car.GetHandle());

		REQUIRE(tree.size() == 3);
		CHECK(tree[0]["components"]["Name"]["Name"] == "Car");
		CHECK(tree[0]["components"]["Hierarchy"]["Parent"] == UUID().ToString());
		CHECK(tree[1]["components"]["Name"]["Name"] == "Wheel");
		CHECK(tree[1]["components"]["Hierarchy"]["Parent"] == test.Car.GetId().ToString());
		CHECK(tree[2]["components"].contains("Follow"));
		CHECK_FALSE(tree[2]["components"].contains("Cache"));
	}

	TEST_CASE("A tree keeps its UUIDs when instanced with IdPolicy::Keep")
	{
		const TestScene test = MakeScene();
		const Json::Value tree = EntitySerializer::SerializeTree(*test.World, test.Car.GetHandle());
		const UUID carId = test.Car.GetId();
		const UUID wheelId = test.Wheel.GetId();
		test.World->DestroyEntity(test.Car);
		REQUIRE(test.World->GetEntityCount() == 1);

		const auto root = EntitySerializer::InstantiateTree(*test.World, tree, EntitySerializer::IdPolicy::Keep);

		REQUIRE_MESSAGE(root.has_value(), Testing::DescribeError(root));
		CHECK(root->GetId() == carId);
		CHECK(test.World->GetEntityCount() == 4);
		const Entity wheel = test.World->FindEntity(wheelId);
		REQUIRE(wheel);
		CHECK(wheel.Get<HierarchyComponent>().Parent == carId);
		CHECK(wheel.GetTransform().Position == glm::vec3(1.0f, 0.0f, 0.0f));
		CHECK(GetNames(*test.World, root->Get<HierarchyComponent>().Children) ==
			std::vector<std::string>{"Wheel", "Driver"});
	}

	TEST_CASE("Instancing a tree with IdPolicy::Keep fails while its UUIDs are in use, changing nothing")
	{
		const TestScene test = MakeScene();
		const Json::Value tree = EntitySerializer::SerializeTree(*test.World, test.Car.GetHandle());

		const auto root = EntitySerializer::InstantiateTree(*test.World, tree, EntitySerializer::IdPolicy::Keep);

		REQUIRE_FALSE(root.has_value());
		CHECK(root.error().GetCode() == ErrorCode::AlreadyExists);
		CHECK(test.World->GetEntityCount() == 4);
	}

	TEST_CASE("A copy gets new UUIDs, and references within it follow the copy")
	{
		const TestScene test = MakeScene();
		const Json::Value tree = EntitySerializer::SerializeTree(*test.World, test.Car.GetHandle());

		const auto copy = EntitySerializer::InstantiateTree(*test.World, tree, EntitySerializer::IdPolicy::Regenerate);

		REQUIRE_MESSAGE(copy.has_value(), Testing::DescribeError(copy));
		CHECK(test.World->GetEntityCount() == 7);
		CHECK(copy->GetId() != test.Car.GetId());
		CHECK(copy->GetName() == "Car");
		const std::vector<UUID> children = copy->Get<HierarchyComponent>().Children;
		REQUIRE(children.size() == 2);
		CHECK(children[0] != test.Wheel.GetId());
		const Entity wheelCopy = test.World->FindEntity(children[0]);
		const Entity driverCopy = test.World->FindEntity(children[1]);
		// The driver's copy follows the wheel's copy; the wheel's copy still follows the road, outside the tree
		CHECK(driverCopy.Get<FollowComponent>().Target == wheelCopy.GetId());
		CHECK(driverCopy.Get<FollowComponent>().Distance == 2.0f);
		CHECK(wheelCopy.Get<FollowComponent>().Target == test.Road.GetId());
		// Internal state isn't copied
		CHECK_FALSE(driverCopy.Has<CacheComponent>());
		// The original is untouched
		CHECK(test.Driver.Get<FollowComponent>().Target == test.Wheel.GetId());
	}

	TEST_CASE("A copy goes under a parent, at a position among its siblings")
	{
		const TestScene test = MakeScene();
		const Json::Value tree = EntitySerializer::SerializeTree(*test.World, test.Wheel.GetHandle());

		const auto copy =
			EntitySerializer::InstantiateTree(*test.World, tree, EntitySerializer::IdPolicy::Regenerate, test.Car, 0);

		REQUIRE(copy.has_value());
		const auto& siblings = test.Car.Get<HierarchyComponent>().Children;
		REQUIRE(siblings.size() == 3);
		CHECK(siblings[0] == copy->GetId());
		// The local transform is kept
		CHECK(copy->GetTransform().Position == glm::vec3(1.0f, 0.0f, 0.0f));
		CHECK(glm::vec3(test.World->GetWorldMatrix(*copy)[3]) == glm::vec3(6.0f, 0.0f, 0.0f));
	}

	TEST_CASE("Only a single tree with its root first can be instanced")
	{
		const TestScene test = MakeScene();
		const Json::Value scene = EntitySerializer::SerializeScene(*test.World);
		Json::Value childFirst = EntitySerializer::SerializeTree(*test.World, test.Car.GetHandle());
		std::swap(childFirst[0], childFirst[1]);

		for (const Json::Value& entities : {scene, childFirst, Json::Value::array()})
		{
			Scene target(GetTestComponents());
			const auto root = EntitySerializer::InstantiateTree(target, entities, EntitySerializer::IdPolicy::Keep);
			REQUIRE_FALSE(root.has_value());
			CHECK(target.GetEntityCount() == 0);
		}
	}

	TEST_CASE("Instancing an invalid tree changes nothing in the scene")
	{
		const TestScene test = MakeScene();
		Json::Value tree = EntitySerializer::SerializeTree(*test.World, test.Car.GetHandle());
		tree[2]["components"]["Follow"]["Distance"] = -1.0;
		const size_t countBefore = test.World->GetEntityCount();

		const auto root = EntitySerializer::InstantiateTree(*test.World, tree, EntitySerializer::IdPolicy::Regenerate);

		REQUIRE_FALSE(root.has_value());
		CHECK(test.World->GetEntityCount() == countBefore);
		CHECK(test.World->GetRootEntities().size() == 2);
	}

	TEST_CASE("Deserializing entities that fail validation leaves no entities behind")
	{
		const TestScene test = MakeScene();
		Json::Value entities = EntitySerializer::SerializeScene(*test.World);
		entities[3]["components"]["Unknown"] = Json::Value::object();
		Scene target(GetTestComponents());
		target.CreateEntity("Existing");

		const auto created = EntitySerializer::DeserializeEntities(target, entities, "entities");

		REQUIRE_FALSE(created.has_value());
		CHECK(target.GetEntityCount() == 1);
		CHECK(target.GetRootEntities().size() == 1);
	}

	TEST_CASE("A parent outside the scene can't be used to instance a tree")
	{
		const TestScene test = MakeScene();
		const Json::Value tree = EntitySerializer::SerializeTree(*test.World, test.Wheel.GetHandle());
		Scene other(GetTestComponents());
		const Entity foreignParent = other.CreateEntity("Elsewhere");

		const auto root =
			EntitySerializer::InstantiateTree(*test.World, tree, EntitySerializer::IdPolicy::Regenerate, foreignParent);

		REQUIRE_FALSE(root.has_value());
		CHECK(root.error().GetCode() == ErrorCode::InvalidArgument);
	}

}
