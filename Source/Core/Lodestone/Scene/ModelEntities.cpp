#include "Lodestone/Scene/ModelEntities.h"

#include "Lodestone/Core/Assert.h"
#include "Lodestone/Scene/Entity.h"
#include "Lodestone/Scene/EntitySerializer.h"
#include "Lodestone/Scene/RenderComponents.h"
#include "Lodestone/Scene/Scene.h"

#include <spdlog/fmt/fmt.h>

#include <string>
#include <utility>
#include <vector>

namespace Lodestone {

	Json::Value BuildModelEntities(
		const Model& model, UUID modelAsset, std::string_view name, const ComponentRegistry& components)
	{
		// Built in a scene of its own, which the entity list is then read from
		Scene scene(components);
		const Entity root = scene.CreateEntity(name);

		std::vector<Entity> entities;
		std::vector<UUID> parents;
		std::vector<bool> placed(model.Nodes.size(), false);
		// Depth first, so each node's entity follows its parent's and siblings keep the model's order
		std::vector<std::pair<uint32_t, UUID>> stack;
		for (auto node = model.RootNodes.rbegin(); node != model.RootNodes.rend(); ++node)
			stack.emplace_back(*node, root.GetId());
		while (!stack.empty())
		{
			const auto [index, parent] = stack.back();
			stack.pop_back();
			if (index >= model.Nodes.size() || placed[index])
				continue;
			placed[index] = true;

			const ModelNode& node = model.Nodes[index];
			Entity entity = scene.CreateEntity(node.Name.empty() ? fmt::format("Node {}", index) : node.Name);
			TransformComponent& transform = entity.GetTransform();
			transform.Position = node.Position;
			transform.Rotation = node.Rotation;
			transform.Scale = node.Scale;
			if (node.Mesh)
				entity.Add<MeshRendererComponent>(MeshRendererComponent{.Model = modelAsset, .Mesh = *node.Mesh});
			entities.push_back(entity);
			parents.push_back(parent);

			for (auto child = node.Children.rbegin(); child != node.Children.rend(); ++child)
				stack.emplace_back(*child, entity.GetId());
		}

		// The entities were all created as roots, so arranging them can't fail
		const auto built = scene.BuildHierarchy(entities, parents);
		LS_CORE_ASSERT(built.has_value(), "Arranging a model's entities failed: {}", built.error());
		return EntitySerializer::SerializeTree(scene, root.GetHandle());
	}

}
