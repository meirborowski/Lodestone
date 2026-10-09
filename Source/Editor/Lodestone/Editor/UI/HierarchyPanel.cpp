#include "Lodestone/Editor/UI/HierarchyPanel.h"

#include "Lodestone/Editor/Commands/EntityCommands.h"
#include "Lodestone/Editor/UI/UIHelpers.h"
#include "Lodestone/Scene/Entity.h"

#include <imgui.h>

#include <algorithm>
#include <string>

namespace Lodestone {

	namespace {

		void CreateEntity(EditorContext& context, UUID parent)
		{
			auto command = CreateScope<CreateEntityCommand>("Entity", parent);
			const CreateEntityCommand& created = *command;
			if (RunCommand(context, std::move(command)))
				context.Select(created.GetEntityId());
		}

		void InstantiatePrefab(EditorContext& context, const std::string& path, UUID parent)
		{
			const auto instance = context.InstantiatePrefab(path, parent);
			if (!instance)
			{
				ReportFailure("Instancing " + path, instance.error());
				return;
			}
			context.Select(*instance);
		}

	}

	void HierarchyPanel::Draw(EditorContext& context, bool* open)
	{
		if (!ImGui::Begin(Title, open))
		{
			ImGui::End();
			return;
		}

		const bool editing = context.GetPlayState() == PlayState::Editing;
		const Scene& scene = context.GetScene();
		// A copy: the roots are drawn while menus may queue changes to them
		const std::vector<UUID> roots(scene.GetRootEntities().begin(), scene.GetRootEntities().end());
		for (const UUID root : roots)
			DrawEntity(context, root);

		// The empty space below the tree: a drop target that moves entities to the root, and the panel's menu
		const ImVec2 available = ImGui::GetContentRegionAvail();
		ImGui::InvisibleButton("##Background", ImVec2(available.x, std::max(available.y, ImGui::GetFrameHeight())));
		if (ImGui::IsItemClicked())
			context.Select({});
		if (editing)
		{
			if (const auto dropped = AcceptEntityDrop())
				Defer([&context, id = *dropped] { RunCommand(context, CreateScope<SetParentCommand>(id, UUID())); });
			if (const auto asset = AcceptAssetDrop())
				Defer([&context, path = *asset] { InstantiatePrefab(context, path, {}); });
		}
		if (ImGui::BeginPopupContextItem("##HierarchyMenu"))
		{
			if (ImGui::MenuItem("Create Entity", nullptr, false, editing))
				Defer([&context] { CreateEntity(context, {}); });
			ImGui::EndPopup();
		}

		ImGui::End();

		std::vector<std::function<void()>> changes;
		changes.swap(m_Deferred);
		for (const std::function<void()>& change : changes)
			change();
	}

	void HierarchyPanel::DrawEntity(EditorContext& context, UUID id)
	{
		const Scene& scene = context.GetScene();
		const entt::entity handle = scene.FindHandle(id);
		if (handle == entt::null)
			return;
		const auto& hierarchy = scene.GetRegistry().get<HierarchyComponent>(handle);
		const bool editing = context.GetPlayState() == PlayState::Editing;

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
			ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
		if (hierarchy.Children.empty())
			flags |= ImGuiTreeNodeFlags_Leaf;
		if (context.GetSelection() == id)
			flags |= ImGuiTreeNodeFlags_Selected;

		const std::string label = GetEntityLabel(scene, id) + "##" + id.ToString();
		const bool expanded = ImGui::TreeNodeEx(label.c_str(), flags);
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
			context.Select(id);
		if (editing)
		{
			SetEntityDragSource(scene, id);
			if (const auto dropped = AcceptEntityDrop(); dropped && *dropped != id)
				Defer([&context, child = *dropped, id]
					{ RunCommand(context, CreateScope<SetParentCommand>(child, id)); });
			if (const auto asset = AcceptAssetDrop())
				Defer([&context, path = *asset, id] { InstantiatePrefab(context, path, id); });
		}
		if (ImGui::BeginPopupContextItem())
		{
			context.Select(id);
			DrawEntityMenu(context, id);
			ImGui::EndPopup();
		}

		if (expanded)
		{
			// A copy, for the same reason as the roots
			const std::vector<UUID> children = hierarchy.Children;
			for (const UUID child : children)
				DrawEntity(context, child);
			ImGui::TreePop();
		}
	}

	void HierarchyPanel::DrawEntityMenu(EditorContext& context, UUID id)
	{
		const bool editing = context.GetPlayState() == PlayState::Editing;
		if (ImGui::MenuItem("Create Child", nullptr, false, editing))
			Defer([&context, id] { CreateEntity(context, id); });
		if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, editing))
		{
			Defer(
				[&context, id]
				{
					auto command = CreateScope<DuplicateEntityCommand>(id);
					const DuplicateEntityCommand& duplicate = *command;
					if (RunCommand(context, std::move(command)))
						context.Select(duplicate.GetCopyId());
				});
		}
		if (ImGui::MenuItem("Unparent", nullptr, false, editing))
			Defer([&context, id] { RunCommand(context, CreateScope<SetParentCommand>(id, UUID())); });
		ImGui::Separator();
		if (ImGui::MenuItem("Delete", "Delete", false, editing))
			Defer([&context, id] { RunCommand(context, CreateScope<DestroyEntityCommand>(id)); });
	}

}
