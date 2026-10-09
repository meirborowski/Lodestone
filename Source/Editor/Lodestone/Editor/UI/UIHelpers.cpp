#include "Lodestone/Editor/UI/UIHelpers.h"

#include "Lodestone/Core/Log.h"
#include "Lodestone/Scene/Entity.h"

#include <imgui.h>

#include <cstring>
#include <type_traits>
#include <utility>

namespace Lodestone {

	static_assert(std::is_trivially_copyable_v<UUID>, "UUIDs are copied into drag and drop payloads");

	void ReportFailure(std::string_view action, const Error& error)
	{
		LS_CORE_ERROR("{} failed: {}", action, error);
	}

	bool RunCommand(EditorContext& context, Scope<Command> command)
	{
		const std::string name = command->GetName();
		if (auto executed = context.Execute(std::move(command)); !executed)
		{
			ReportFailure(name, executed.error());
			return false;
		}
		return true;
	}

	std::string GetEntityLabel(const Scene& scene, UUID id)
	{
		const entt::entity handle = scene.FindHandle(id);
		if (handle == entt::null)
			return "(missing entity)";
		const std::string& name = scene.GetRegistry().get<NameComponent>(handle).Name;
		return name.empty() ? std::string("(unnamed)") : name;
	}

	void SetEntityDragSource(const Scene& scene, UUID id)
	{
		if (ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload(EntityPayload, &id, sizeof(id));
			ImGui::TextUnformatted(GetEntityLabel(scene, id).c_str());
			ImGui::EndDragDropSource();
		}
	}

	std::optional<UUID> AcceptEntityDrop()
	{
		std::optional<UUID> dropped;
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EntityPayload);
				payload != nullptr && payload->DataSize == sizeof(UUID))
			{
				UUID id;
				std::memcpy(&id, payload->Data, sizeof(id));
				dropped = id;
			}
			ImGui::EndDragDropTarget();
		}
		return dropped;
	}

	std::optional<std::string> AcceptAssetDrop()
	{
		std::optional<std::string> dropped;
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetPayload);
				payload != nullptr && payload->DataSize > 0)
			{
				dropped.emplace(static_cast<const char*>(payload->Data), static_cast<size_t>(payload->DataSize));
			}
			ImGui::EndDragDropTarget();
		}
		return dropped;
	}

}
