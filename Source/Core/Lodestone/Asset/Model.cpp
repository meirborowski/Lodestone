#include "Lodestone/Asset/Model.h"

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <utility>
#include <vector>

namespace Lodestone {

	BoundingBox Model::GetBounds() const
	{
		BoundingBox bounds;
		// Depth-first without recursion; a node is visited at most once, whatever the hierarchy claims
		std::vector<bool> visited(Nodes.size(), false);
		std::vector<std::pair<uint32_t, glm::mat4>> pending;
		pending.reserve(RootNodes.size());
		for (const uint32_t root : RootNodes)
			pending.emplace_back(root, glm::mat4(1.0f));
		while (!pending.empty())
		{
			const auto [index, parentMatrix] = pending.back();
			pending.pop_back();
			if (index >= Nodes.size() || visited[index])
				continue;
			visited[index] = true;
			const ModelNode& node = Nodes[index];
			const glm::mat4 matrix = parentMatrix * glm::translate(glm::mat4(1.0f), node.Position) *
				glm::mat4_cast(node.Rotation) * glm::scale(glm::mat4(1.0f), node.Scale);
			if (node.Mesh && *node.Mesh < Meshes.size())
				bounds.Add(Meshes[*node.Mesh].Bounds.Transform(matrix));
			for (const uint32_t child : node.Children)
				pending.emplace_back(child, matrix);
		}
		return bounds;
	}

}
