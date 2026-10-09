#pragma once

#include "Lodestone/Core/UUID.h"
#include "Lodestone/Editor/EditorContext.h"
#include "Lodestone/Reflection/ComponentRegistry.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <optional>
#include <string>

namespace Lodestone {

	// The selected entity's components, with a widget for each field, driven by the reflection registry. Edits are
	// commands: dragging a value is one undo step. Read-only while playing
	class InspectorPanel
	{
	public:
		static constexpr const char* Title = "Inspector";

		void Draw(EditorContext& context, bool* open);

	private:
		void DrawComponent(EditorContext& context, UUID id, const ComponentType& type, bool editing);
		// Draws a field's widget. Returns whether the value changed
		bool DrawField(UUID id, const ComponentType& type, const FieldInfo& field, FieldValue& value);
		void DrawAddComponent(EditorContext& context, UUID id);

	private:
		// Rotations show as Euler angles in degrees. Converting a quaternion back gives different angles for the same
		// rotation, so the angles being edited are kept while the rotation is still the one they produced
		struct EulerAngles
		{
			UUID Entity;
			std::string Field;
			glm::quat Rotation;
			glm::vec3 Degrees;
		};
		std::optional<EulerAngles> m_Euler;
	};

}
