#include "Lodestone/Scene/Scene.h"

#include "Lodestone/Scene/Entity.h"

#include <doctest/doctest.h>
#include <glm/gtc/matrix_transform.hpp>

#include <vector>

namespace Lodestone {

	namespace {

		std::vector<UUID> IdsOf(std::initializer_list<Entity> entities)
		{
			std::vector<UUID> ids;
			for (const Entity& entity : entities)
				ids.push_back(entity.GetId());
			return ids;
		}

		std::vector<UUID> ChildrenOf(Entity entity)
		{
			return entity.Get<HierarchyComponent>().Children;
		}

		bool Near(const glm::vec4& a, const glm::vec4& b)
		{
			return glm::all(glm::lessThan(glm::abs(a - b), glm::vec4(1e-5f)));
		}

	}

	TEST_CASE("New entities have the required components and are root entities")
	{
		Scene scene;
		Entity entity = scene.CreateEntity("Player");

		CHECK(entity.IsValid());
		CHECK(entity.GetName() == "Player");
		CHECK_FALSE(entity.GetId().IsNil());
		CHECK(entity.Has<IDComponent>());
		CHECK(entity.Has<TransformComponent>());
		CHECK(entity.Get<HierarchyComponent>().Parent.IsNil());
		CHECK(entity.GetTransform().Position == glm::vec3(0.0f));
		CHECK(entity.GetTransform().Scale == glm::vec3(1.0f));
		CHECK(scene.GetEntityCount() == 1);
		CHECK(std::vector(scene.GetRootEntities().begin(), scene.GetRootEntities().end()) == IdsOf({entity}));
		CHECK(scene.FindEntity(entity.GetId()) == entity);
	}

	TEST_CASE("Entities can be created with a given UUID, which must be unique and not nil")
	{
		Scene scene;
		const UUID id(7, 9);
		const auto created = scene.CreateEntityWithId(id, "Fixed");
		REQUIRE(created.has_value());
		CHECK(created->GetId() == id);

		const auto duplicate = scene.CreateEntityWithId(id);
		REQUIRE_FALSE(duplicate.has_value());
		CHECK(duplicate.error().GetCode() == ErrorCode::AlreadyExists);
		const auto nil = scene.CreateEntityWithId(UUID());
		REQUIRE_FALSE(nil.has_value());
		CHECK(nil.error().GetCode() == ErrorCode::InvalidArgument);
		CHECK(scene.GetEntityCount() == 1);
	}

	TEST_CASE("Components are added, read and removed through entities")
	{
		struct Health
		{
			int Value = 100;
		};

		Scene scene;
		Entity entity = scene.CreateEntity();
		CHECK_FALSE(entity.Has<Health>());
		CHECK(entity.TryGet<Health>() == nullptr);

		entity.Add<Health>(42);
		CHECK(entity.Get<Health>().Value == 42);
		entity.Get<Health>().Value = 7;
		CHECK(entity.TryGet<Health>()->Value == 7);

		entity.Remove<Health>();
		CHECK_FALSE(entity.Has<Health>());
	}

	TEST_CASE("SetParent moves entities in the hierarchy and keeps both sides consistent")
	{
		Scene scene;
		const Entity parent = scene.CreateEntity("Parent");
		Entity first = scene.CreateEntity("First");
		const Entity second = scene.CreateEntity("Second");

		REQUIRE(scene.SetParent(second, parent).has_value());
		REQUIRE(scene.SetParent(first, parent, 0).has_value());

		CHECK(ChildrenOf(parent) == IdsOf({first, second}));
		CHECK(first.Get<HierarchyComponent>().Parent == parent.GetId());
		CHECK(std::vector(scene.GetRootEntities().begin(), scene.GetRootEntities().end()) == IdsOf({parent}));

		SUBCASE("Back to the root")
		{
			REQUIRE(scene.SetParent(first, Entity()).has_value());
			CHECK(ChildrenOf(parent) == IdsOf({second}));
			CHECK(first.Get<HierarchyComponent>().Parent.IsNil());
			CHECK(
				std::vector(scene.GetRootEntities().begin(), scene.GetRootEntities().end()) == IdsOf({parent, first}));
		}
		SUBCASE("Reordering among siblings")
		{
			REQUIRE(scene.SetParent(first, parent, 5).has_value());
			CHECK(ChildrenOf(parent) == IdsOf({second, first}));
		}
	}

	TEST_CASE("SetParent rejects cycles")
	{
		Scene scene;
		const Entity top = scene.CreateEntity();
		const Entity middle = scene.CreateEntity();
		const Entity bottom = scene.CreateEntity();
		REQUIRE(scene.SetParent(middle, top).has_value());
		REQUIRE(scene.SetParent(bottom, middle).has_value());

		CHECK_FALSE(scene.SetParent(top, bottom).has_value());
		CHECK_FALSE(scene.SetParent(top, top).has_value());
		// Nothing changed
		CHECK(top.Get<HierarchyComponent>().Parent.IsNil());
		CHECK(ChildrenOf(bottom).empty());
	}

	TEST_CASE("SetParent rejects entities from other scenes")
	{
		Scene scene;
		Scene other;
		const Entity local = scene.CreateEntity();
		const Entity stranger = other.CreateEntity();

		CHECK_FALSE(scene.SetParent(local, stranger).has_value());
		CHECK_FALSE(scene.SetParent(stranger, local).has_value());
	}

	TEST_CASE("BuildHierarchy arranges many entities at once, keeping their order")
	{
		Scene scene;
		const Entity existing = scene.CreateEntity("Existing");
		std::vector<Entity> entities;
		entities.reserve(5);
		for (int i = 0; i < 5; ++i)
			entities.push_back(scene.CreateEntity());
		// 0 and 2 are children of 1; 3 is a child of 0; 4 goes under an entity that was already in the scene
		const std::vector<UUID> parents = {
			entities[1].GetId(), UUID(), entities[1].GetId(), entities[0].GetId(), existing.GetId()};

		REQUIRE(scene.BuildHierarchy(entities, parents).has_value());

		CHECK(ChildrenOf(entities[1]) == IdsOf({entities[0], entities[2]}));
		CHECK(ChildrenOf(entities[0]) == IdsOf({entities[3]}));
		CHECK(ChildrenOf(existing) == IdsOf({entities[4]}));
		CHECK(entities[3].Get<HierarchyComponent>().Parent == entities[0].GetId());
		CHECK(std::vector(scene.GetRootEntities().begin(), scene.GetRootEntities().end()) ==
			IdsOf({existing, entities[1]}));
	}

	TEST_CASE("BuildHierarchy rejects cycles and missing parents, changing nothing")
	{
		Scene scene;
		std::vector<Entity> entities;
		entities.reserve(3);
		for (int i = 0; i < 3; ++i)
			entities.push_back(scene.CreateEntity());
		const auto expectRejected = [&](const std::vector<UUID>& parents)
		{
			const auto built = scene.BuildHierarchy(entities, parents);
			REQUIRE_FALSE(built.has_value());
			CHECK(built.error().GetCode() == ErrorCode::InvalidArgument);
			CHECK(scene.GetRootEntities().size() == 3);
			for (const Entity& entity : entities)
				CHECK(entity.Get<HierarchyComponent>().Parent.IsNil());
		};

		expectRejected({entities[1].GetId(), entities[2].GetId(), entities[0].GetId()});
		expectRejected({entities[0].GetId(), UUID(), UUID()});
		expectRejected({UUID(), UUID(), UUID(42, 42)});
	}

	TEST_CASE("BuildHierarchy handles a very deep hierarchy quickly")
	{
		// A chain this deep from a hostile file must not take quadratic time or recurse
		constexpr size_t depth = 100000;
		Scene scene;
		std::vector<Entity> entities;
		std::vector<UUID> parents;
		entities.reserve(depth);
		parents.reserve(depth);
		for (size_t i = 0; i < depth; ++i)
		{
			entities.push_back(scene.CreateEntity());
			parents.push_back(i == 0 ? UUID() : entities[i - 1].GetId());
		}

		REQUIRE(scene.BuildHierarchy(entities, parents).has_value());
		CHECK(scene.GetRootEntities().size() == 1);
		CHECK(ChildrenOf(entities[depth - 2]) == IdsOf({entities[depth - 1]}));

		// Destroying the root takes the whole chain with it, without recursion
		scene.DestroyEntity(entities[0]);
		CHECK(scene.GetEntityCount() == 0);
	}

	TEST_CASE("Destroying an entity destroys its descendants and detaches it from its parent")
	{
		Scene scene;
		const Entity root = scene.CreateEntity();
		const Entity middle = scene.CreateEntity();
		const Entity leaf = scene.CreateEntity();
		const Entity sibling = scene.CreateEntity();
		REQUIRE(scene.SetParent(middle, root).has_value());
		REQUIRE(scene.SetParent(leaf, middle).has_value());
		REQUIRE(scene.SetParent(sibling, root).has_value());
		const UUID leafId = leaf.GetId();

		scene.DestroyEntity(middle);

		CHECK_FALSE(middle.IsValid());
		CHECK_FALSE(leaf.IsValid());
		CHECK_FALSE(scene.Contains(leafId));
		CHECK_FALSE(scene.FindEntity(leafId).IsValid());
		CHECK(ChildrenOf(root) == IdsOf({sibling}));
		CHECK(scene.GetEntityCount() == 2);
	}

	TEST_CASE("World matrices combine the transforms of every ancestor")
	{
		Scene scene;
		Entity parent = scene.CreateEntity();
		Entity child = scene.CreateEntity();
		REQUIRE(scene.SetParent(child, parent).has_value());
		parent.GetTransform().Position = glm::vec3(10.0f, 0.0f, 0.0f);
		parent.GetTransform().Rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		parent.GetTransform().Scale = glm::vec3(2.0f);
		child.GetTransform().Position = glm::vec3(1.0f, 0.0f, 0.0f);

		// The child's origin: scaled by 2, rotated 90 degrees about Y (+X becomes -Z), then moved to the parent
		const glm::vec4 origin = scene.GetWorldMatrix(child) * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		CAPTURE(origin.x);
		CAPTURE(origin.z);
		CHECK(Near(origin, glm::vec4(10.0f, 0.0f, -2.0f, 1.0f)));
		CHECK(
			Near(scene.GetWorldMatrix(parent) * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), glm::vec4(10.0f, 0.0f, 0.0f, 1.0f)));
	}

	TEST_CASE("A snapshot restores the scene exactly, entity handles included")
	{
		Scene scene;
		const Entity parent = scene.CreateEntity("Parent");
		Entity child = scene.CreateEntity("Child");
		REQUIRE(scene.SetParent(child, parent).has_value());
		child.GetTransform().Position = glm::vec3(1.0f, 2.0f, 3.0f);
		scene.DestroyEntity(scene.CreateEntity("Destroyed"));

		const SceneSnapshot snapshot = scene.SaveSnapshot();
		const entt::entity childHandle = child.GetHandle();

		// Changes after the snapshot: an entity created, one destroyed, a component edited
		const Entity added = scene.CreateEntity("Added");
		const entt::entity addedHandle = added.GetHandle();
		const UUID addedId = added.GetId();
		child.GetTransform().Position = glm::vec3(9.0f);
		scene.DestroyEntity(parent);

		scene.RestoreSnapshot(snapshot);

		CHECK(scene.GetEntityCount() == 2);
		Entity restoredChild = scene.FindEntity(child.GetId());
		REQUIRE(restoredChild.IsValid());
		CHECK(restoredChild.GetHandle() == childHandle);
		CHECK(restoredChild.GetName() == "Child");
		CHECK(restoredChild.GetTransform().Position == glm::vec3(1.0f, 2.0f, 3.0f));
		CHECK(restoredChild.Get<HierarchyComponent>().Parent == parent.GetId());
		CHECK_FALSE(scene.Contains(addedId));
		CHECK(std::vector(scene.GetRootEntities().begin(), scene.GetRootEntities().end()) == IdsOf({parent}));

		// The next entity gets the same handle as the one created after the snapshot did
		CHECK(scene.CreateEntity("Again").GetHandle() == addedHandle);
	}

}
