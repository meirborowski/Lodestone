#pragma once

#include "Lodestone/Core/UUID.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <string>
#include <vector>

// The core components every entity has. Each is registered with the reflection registry in RegisterCoreComponents()
// (see docs/Architecture.md#components-and-reflection, and AGENTS.md for adding a component)

namespace Lodestone {

	// The entity's stable identity, which files, prefabs and the network refer to it by
	struct IDComponent
	{
		UUID ID;
	};

	struct NameComponent
	{
		std::string Name;
	};

	// Position, rotation and scale relative to the parent entity (or the world, for root entities)
	struct TransformComponent
	{
		glm::vec3 Position{0.0f};
		glm::quat Rotation = glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f);
		glm::vec3 Scale{1.0f};

		// The local transform as a matrix: scale, then rotation, then translation
		glm::mat4 GetMatrix() const;
	};

	// The entity's place in the scene hierarchy. Only Scene changes it, which keeps parents and children consistent
	// (see Scene::SetParent)
	struct HierarchyComponent
	{
		// The nil UUID for root entities
		UUID Parent;
		// In order. Rebuilt from the children's parents when a scene loads, so it isn't saved
		std::vector<UUID> Children;
	};

	// The transform at the start of the latest simulation tick, which rendering interpolates from (see Simulation)
	struct PreviousTransformComponent
	{
		TransformComponent Transform;
	};

	class ComponentRegistry;

	// Registers the core components above
	void RegisterCoreComponents(ComponentRegistry& registry);

	// The engine's component registry, which scenes use unless given another. The core components are registered when
	// it's first used; engine modules register theirs at startup, before any scene uses them
	ComponentRegistry& GetEngineComponentRegistry();

}
