#include "Lodestone/Editor/UI/InspectorPanel.h"

#include "Lodestone/Core/Log.h"
#include "Lodestone/Editor/Commands/EntityCommands.h"
#include "Lodestone/Editor/UI/UIHelpers.h"
#include "Lodestone/Serialization/Json.h"

#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <limits>

namespace Lodestone {

	namespace {

		constexpr float FloatDragSpeed = 0.01f;
		constexpr float IntegerDragSpeed = 0.1f;
		constexpr float AngleDragSpeed = 0.5f;

		float GetFloatMin(const FieldInfo& field)
		{
			return field.GetMin() ? static_cast<float>(*field.GetMin()) : -FLT_MAX;
		}

		float GetFloatMax(const FieldInfo& field)
		{
			return field.GetMax() ? static_cast<float>(*field.GetMax()) : FLT_MAX;
		}

		template <typename T>
		T GetIntegerLimit(std::optional<double> limit, T fallback)
		{
			if (!limit)
				return fallback;
			const double clamped = std::clamp(*limit, static_cast<double>(std::numeric_limits<T>::lowest()),
				static_cast<double>(std::numeric_limits<T>::max()));
			return static_cast<T>(clamped);
		}

		ImGuiSliderFlags GetClampFlags(const FieldInfo& field)
		{
			return field.GetMin() || field.GetMax() ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None;
		}

	}

	void InspectorPanel::Draw(EditorContext& context, bool* open)
	{
		if (!ImGui::Begin(Title, open))
		{
			ImGui::End();
			return;
		}

		const UUID id = context.GetSelection();
		if (id.IsNil())
		{
			ImGui::TextDisabled("Select an entity to inspect it");
			ImGui::End();
			return;
		}

		const bool editing = context.GetPlayState() == PlayState::Editing;
		if (!editing)
			ImGui::TextDisabled("Stop playing to edit");

		const Scene& scene = context.GetScene();
		const entt::entity handle = scene.FindHandle(id);
		for (const ComponentType* type : scene.GetComponentRegistry().GetTypes())
		{
			if (!type->IsInternal() && type->Has(scene.GetRegistry(), handle))
				DrawComponent(context, id, *type, editing);
			// A command may have removed the entity, e.g. an undo from a shortcut; nothing more to draw then
			if (!context.GetScene().Contains(id))
				break;
		}

		if (editing && context.GetScene().Contains(id))
		{
			ImGui::Spacing();
			DrawAddComponent(context, id);
		}
		ImGui::End();
	}

	void InspectorPanel::DrawComponent(EditorContext& context, UUID id, const ComponentType& type, bool editing)
	{
		ImGui::PushID(type.GetName().c_str());
		const bool open = ImGui::CollapsingHeader(type.GetName().c_str(), ImGuiTreeNodeFlags_DefaultOpen);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !type.GetDescription().empty())
			ImGui::SetTooltip("%s", type.GetDescription().c_str());

		bool removed = false;
		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("Remove Component", nullptr, false, editing && !type.IsRequired()))
				removed = RunCommand(context, CreateScope<RemoveComponentCommand>(id, type.GetName()));
			ImGui::EndPopup();
		}

		if (open && !removed)
		{
			const Scene& scene = context.GetScene();
			const void* component = type.TryGet(scene.GetRegistry(), scene.FindHandle(id));
			if (ImGui::BeginTable("##Fields", 2, ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.35f);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.65f);
				for (const FieldInfo& field : type.GetFields())
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(field.GetName().c_str());
					if (ImGui::IsItemHovered() && !field.GetDescription().empty())
						ImGui::SetTooltip("%s", field.GetDescription().c_str());

					ImGui::TableNextColumn();
					ImGui::SetNextItemWidth(-FLT_MIN);
					ImGui::BeginDisabled(!editing || field.IsReadOnly());
					FieldValue value = field.Get(component);
					const bool changed = DrawField(id, type, field, value);
					const bool finished = ImGui::IsItemDeactivatedAfterEdit();
					ImGui::EndDisabled();

					if (changed)
					{
						// A drag sends a command each frame, and they merge into one undo step until it ends
						Json::Value fields = Json::Value::object();
						fields[field.GetName()] = Json::FromFieldValue(value);
						RunCommand(context, CreateScope<SetFieldsCommand>(id, type.GetName(), std::move(fields), true));
						// The command may have changed what the component points to; draw the rest next frame
						component = type.TryGet(context.GetScene().GetRegistry(), context.GetScene().FindHandle(id));
						if (component == nullptr)
							break;
					}
					if (finished || (changed && !ImGui::IsItemActive()))
						context.GetHistory().EndMerge();
				}
				ImGui::EndTable();
			}
		}
		ImGui::PopID();
	}

	bool InspectorPanel::DrawField(UUID id, const ComponentType& type, const FieldInfo& field, FieldValue& value)
	{
		const std::string label = "##" + field.GetName();
		switch (field.GetType())
		{
			case FieldType::Bool:
				return ImGui::Checkbox(label.c_str(), &std::get<bool>(value));
			case FieldType::Int: {
				const auto min = GetIntegerLimit<int32_t>(field.GetMin(), std::numeric_limits<int32_t>::lowest());
				const auto max = GetIntegerLimit<int32_t>(field.GetMax(), std::numeric_limits<int32_t>::max());
				return ImGui::DragScalar(label.c_str(), ImGuiDataType_S32, &std::get<int32_t>(value), IntegerDragSpeed,
					&min, &max, nullptr, GetClampFlags(field));
			}
			case FieldType::UInt: {
				const auto min = GetIntegerLimit<uint32_t>(field.GetMin(), 0);
				const auto max = GetIntegerLimit<uint32_t>(field.GetMax(), std::numeric_limits<uint32_t>::max());
				return ImGui::DragScalar(label.c_str(), ImGuiDataType_U32, &std::get<uint32_t>(value), IntegerDragSpeed,
					&min, &max, nullptr, GetClampFlags(field));
			}
			case FieldType::Float:
				return ImGui::DragFloat(label.c_str(), &std::get<float>(value), FloatDragSpeed, GetFloatMin(field),
					GetFloatMax(field), "%.3f", GetClampFlags(field));
			case FieldType::Vec2:
				return ImGui::DragFloat2(label.c_str(), glm::value_ptr(std::get<glm::vec2>(value)), FloatDragSpeed,
					GetFloatMin(field), GetFloatMax(field), "%.3f", GetClampFlags(field));
			case FieldType::Vec3:
				return ImGui::DragFloat3(label.c_str(), glm::value_ptr(std::get<glm::vec3>(value)), FloatDragSpeed,
					GetFloatMin(field), GetFloatMax(field), "%.3f", GetClampFlags(field));
			case FieldType::Vec4:
				return ImGui::DragFloat4(label.c_str(), glm::value_ptr(std::get<glm::vec4>(value)), FloatDragSpeed,
					GetFloatMin(field), GetFloatMax(field), "%.3f", GetClampFlags(field));
			case FieldType::Quat: {
				auto& rotation = std::get<glm::quat>(value);
				const std::string key = type.GetName() + "." + field.GetName();
				if (!m_Euler || m_Euler->Entity != id || m_Euler->Field != key || m_Euler->Rotation != rotation)
					m_Euler = EulerAngles{.Entity = id,
						.Field = key,
						.Rotation = rotation,
						// Adding zero turns -0 into 0, which shows better
						.Degrees = glm::degrees(glm::eulerAngles(rotation)) + glm::vec3(0.0f)};
				if (!ImGui::DragFloat3(
						label.c_str(), glm::value_ptr(m_Euler->Degrees), AngleDragSpeed, 0.0f, 0.0f, "%.1f\xC2\xB0"))
					return false;
				rotation = glm::normalize(glm::quat(glm::radians(m_Euler->Degrees)));
				m_Euler->Rotation = rotation;
				return true;
			}
			case FieldType::String:
				return ImGui::InputText(label.c_str(), &std::get<std::string>(value));
			case FieldType::UUID: {
				// Edited as text, applied once it's a whole UUID (or empty, for nil)
				const UUID current = std::get<UUID>(value);
				std::string text = current.IsNil() ? std::string() : current.ToString();
				if (!ImGui::InputTextWithHint(label.c_str(), "nil", &text, ImGuiInputTextFlags_EnterReturnsTrue))
					return false;
				if (text.empty())
				{
					value = UUID();
					return !current.IsNil();
				}
				const std::optional<UUID> parsed = UUID::Parse(text);
				if (!parsed)
				{
					LS_CORE_WARN("'{}' isn't a UUID", text);
					return false;
				}
				value = *parsed;
				return *parsed != current;
			}
		}
		return false;
	}

	void InspectorPanel::DrawAddComponent(EditorContext& context, UUID id)
	{
		const float width = ImGui::GetContentRegionAvail().x;
		if (ImGui::Button("Add Component", ImVec2(width, 0.0f)))
			ImGui::OpenPopup("##AddComponent");
		if (ImGui::BeginPopup("##AddComponent"))
		{
			const Scene& scene = context.GetScene();
			const entt::entity handle = scene.FindHandle(id);
			bool any = false;
			for (const ComponentType* type : scene.GetComponentRegistry().GetTypes())
			{
				if (type->IsInternal() || type->Has(scene.GetRegistry(), handle))
					continue;
				any = true;
				if (ImGui::MenuItem(type->GetName().c_str()))
				{
					RunCommand(context, CreateScope<AddComponentCommand>(id, type->GetName()));
					break;
				}
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !type->GetDescription().empty())
					ImGui::SetTooltip("%s", type->GetDescription().c_str());
			}
			if (!any)
				ImGui::TextDisabled("The entity has every component");
			ImGui::EndPopup();
		}
	}

}
