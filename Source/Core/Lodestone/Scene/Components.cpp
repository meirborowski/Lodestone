#include "Lodestone/Scene/Components.h"

#include "Lodestone/Reflection/ComponentRegistry.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Lodestone {

	glm::mat4 TransformComponent::GetMatrix() const
	{
		const glm::mat4 translation = glm::translate(glm::mat4(1.0f), Position);
		return translation * glm::mat4_cast(Rotation) * glm::scale(glm::mat4(1.0f), Scale);
	}

	void RegisterCoreComponents(ComponentRegistry& registry)
	{
		registry
			.Register<IDComponent>("ID", "The entity's stable identity, which files and the network refer to it by",
				ComponentFlags::Required)
			.Field("ID", &IDComponent::ID, {.Description = "Unique and permanent", .Flags = FieldFlags::ReadOnly});

		registry.Register<NameComponent>("Name", "The entity's display name", ComponentFlags::Required)
			.Field("Name", &NameComponent::Name, {.Description = "Doesn't need to be unique"});

		registry
			.Register<TransformComponent>(
				"Transform", "Position, rotation and scale relative to the parent entity", ComponentFlags::Required)
			.Field("Position", &TransformComponent::Position,
				{.Description = "In meters, relative to the parent", .Flags = FieldFlags::Replicated})
			.Field("Rotation", &TransformComponent::Rotation,
				{.Description = "A unit quaternion, relative to the parent", .Flags = FieldFlags::Replicated})
			.Field("Scale", &TransformComponent::Scale,
				{.Description = "Relative to the parent", .Flags = FieldFlags::Replicated});

		// Parents change through Scene::SetParent, which keeps both sides of the relationship consistent, so the field
		// is read-only to editing tools
		registry
			.Register<HierarchyComponent>(
				"Hierarchy", "The entity's place in the scene hierarchy", ComponentFlags::Required)
			.Field("Parent", &HierarchyComponent::Parent,
				{.Description = "The parent entity, or nil for a root entity",
					.Flags = FieldFlags::ReadOnly | FieldFlags::Replicated});

		registry.Register<PreviousTransformComponent>("PreviousTransform",
			"The transform at the start of the latest simulation tick, for interpolation", ComponentFlags::Internal);
	}

	ComponentRegistry& GetEngineComponentRegistry()
	{
		// Intentionally never destroyed: scenes may outlive static destruction order
		static ComponentRegistry* s_Registry = []
		{
			auto* registry = new ComponentRegistry();
			RegisterCoreComponents(*registry);
			return registry;
		}();
		return *s_Registry;
	}

}
