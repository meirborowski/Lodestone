#include "Lodestone/Scene/ModelEntities.h"

#include "Common/DescribeError.h"
#include "Lodestone/Animation/AnimationComponents.h"
#include "Lodestone/Asset/ModelImporter.h"
#include "Lodestone/Scene/Entity.h"
#include "Lodestone/Scene/EntitySerializer.h"
#include "Lodestone/Scene/RenderComponents.h"
#include "Lodestone/Scene/Scene.h"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace Lodestone {

	namespace {

		constexpr UUID ModelAsset(0x3000000000004000ull, 0x8000000000000001ull);

		// Copies a model's entities into a scene, and returns its root
		Entity Instantiate(Scene& scene, const Model& model)
		{
			const Json::Value entities = BuildModelEntities(model, ModelAsset, "Hierarchy");
			auto root = EntitySerializer::InstantiateTree(scene, entities, EntitySerializer::IdPolicy::Regenerate);
			REQUIRE_MESSAGE(root.has_value(), Testing::DescribeError(root));
			return *root;
		}

		std::vector<std::string> GetChildNames(Scene& scene, Entity entity)
		{
			std::vector<std::string> names;
			for (const UUID child : entity.Get<HierarchyComponent>().Children)
				names.push_back(scene.FindEntity(child).GetName());
			return names;
		}

	}

	TEST_CASE("A model's entities follow its node hierarchy under a root named after it")
	{
		const auto model =
			ModelImporter::Import(std::filesystem::path(LS_TEST_FIXTURE_DIR) / "Models" / "Hierarchy.gltf");
		REQUIRE_MESSAGE(model.has_value(), Testing::DescribeError(model));
		Scene scene;

		const Entity root = Instantiate(scene, *model);

		CHECK(root.GetName() == "Hierarchy");
		CHECK(root.TryGet<MeshRendererComponent>() == nullptr);
		CHECK(scene.GetEntityCount() == 6);
		CHECK(GetChildNames(scene, root) == std::vector<std::string>{"Root", "Plain"});
		const Entity node = scene.FindEntity(root.Get<HierarchyComponent>().Children[0]);
		CHECK(GetChildNames(scene, node) == std::vector<std::string>{"Turned", "Marker"});
		CHECK(node.GetTransform().Position == glm::vec3(1.0f, 0.0f, 0.0f));
		const Entity turned = scene.FindEntity(node.Get<HierarchyComponent>().Children[0]);
		CHECK(glm::length(turned.GetTransform().Scale - glm::vec3(2.0f)) < 1e-5f);
		CHECK(turned.TryGet<MeshRendererComponent>() == nullptr);

		// Nodes with meshes draw them, from the model asset
		const Entity parts = scene.FindEntity(turned.Get<HierarchyComponent>().Children[0]);
		CHECK(parts.GetName() == "Parts");
		REQUIRE(parts.TryGet<MeshRendererComponent>() != nullptr);
		CHECK(parts.Get<MeshRendererComponent>().Model == ModelAsset);
		CHECK(parts.Get<MeshRendererComponent>().Mesh == 0);
		const Entity plain = scene.FindEntity(root.Get<HierarchyComponent>().Children[1]);
		REQUIRE(plain.TryGet<MeshRendererComponent>() != nullptr);
		CHECK(plain.Get<MeshRendererComponent>().Mesh == 1);
		// The placed meshes end up where the model puts them
		CHECK(glm::length(glm::vec3(scene.GetWorldMatrix(plain)[3]) - glm::vec3(0.0f, 0.0f, -2.0f)) < 1e-5f);
	}

	TEST_CASE("A model's entities know their model and nodes, skins and clips")
	{
		const auto model =
			ModelImporter::Import(std::filesystem::path(LS_TEST_FIXTURE_DIR) / "Models" / "SkinnedArm.glb");
		REQUIRE_MESSAGE(model.has_value(), Testing::DescribeError(model));
		Scene scene;

		const Entity root = Instantiate(scene, *model);

		// The root is the instance, and plays the model's first clip
		REQUIRE(root.TryGet<ModelInstanceComponent>() != nullptr);
		CHECK(root.Get<ModelInstanceComponent>().Model == ModelAsset);
		REQUIRE(root.TryGet<AnimatorComponent>() != nullptr);
		CHECK(root.Get<AnimatorComponent>().Clip == 0);
		CHECK(root.Get<AnimatorComponent>().Playing);
		CHECK(root.TryGet<ModelNodeComponent>() == nullptr);
		CHECK(GetChildNames(scene, root) == std::vector<std::string>{"Arm", "Lower"});

		// Each node's entity knows its node; the mesh's knows its skin
		const Entity arm = scene.FindEntity(root.Get<HierarchyComponent>().Children[0]);
		REQUIRE(arm.TryGet<ModelNodeComponent>() != nullptr);
		CHECK(arm.Get<ModelNodeComponent>().Node == 0);
		REQUIRE(arm.TryGet<SkinComponent>() != nullptr);
		CHECK(arm.Get<SkinComponent>().Skin == 0);
		const Entity lower = scene.FindEntity(root.Get<HierarchyComponent>().Children[1]);
		CHECK(lower.Get<ModelNodeComponent>().Node == 1);
		CHECK(lower.TryGet<SkinComponent>() == nullptr);
		const Entity upper = scene.FindEntity(lower.Get<HierarchyComponent>().Children[0]);
		CHECK(upper.Get<ModelNodeComponent>().Node == 2);

		// A model without clips gets no animator
		Scene still;
		const Entity stillRoot = Instantiate(still, Model());
		CHECK(stillRoot.TryGet<ModelInstanceComponent>() != nullptr);
		CHECK(stillRoot.TryGet<AnimatorComponent>() == nullptr);
	}

	TEST_CASE("Unnamed nodes are named by index, and nodes reached twice are placed once")
	{
		Model model;
		model.Meshes.emplace_back();
		model.Nodes.push_back({.Name = "", .Children = {1}});
		model.Nodes.push_back({.Name = "Shared", .Mesh = 0});
		// Both roots list the shared node, and the first lists it as a child too
		model.RootNodes = {0, 1, 7};
		Scene scene;

		const Entity root = Instantiate(scene, model);

		CHECK(GetChildNames(scene, root) == std::vector<std::string>{"Node 0"});
		const Entity first = scene.FindEntity(root.Get<HierarchyComponent>().Children[0]);
		CHECK(GetChildNames(scene, first) == std::vector<std::string>{"Shared"});
		CHECK(scene.GetEntityCount() == 3);
	}

	TEST_CASE("An empty model is just its root")
	{
		Scene scene;

		const Entity root = Instantiate(scene, Model());

		CHECK(root.GetName() == "Hierarchy");
		CHECK(scene.GetEntityCount() == 1);
	}

}
