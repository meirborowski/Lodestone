#include "Lodestone/Editor/UI/EditorUI.h"

#include "Lodestone/Core/FileSystem.h"
#include "Lodestone/Core/Log.h"
#include "Lodestone/Editor/Commands/EntityCommands.h"
#include "Lodestone/Editor/UI/UIHelpers.h"

#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <ImGuizmo.h>
#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace Lodestone {

	namespace {

		constexpr const char* DockSpaceName = "LodestoneDockSpace";
		constexpr const char* NewProjectTitle = "New Project";
		constexpr const char* OpenProjectTitle = "Open Project";
		constexpr const char* SaveAsTitle = "Save As";
		constexpr const char* CreatePrefabTitle = "Create Prefab";
		constexpr const char* ConfirmDiscardTitle = "Unsaved Changes";
		constexpr ImVec4 ErrorColor{1.0f, 0.4f, 0.4f, 1.0f};

		bool IsEditing(const EditorContext& context)
		{
			return context.GetPlayState() == PlayState::Editing;
		}

		std::string GetDocumentName(const EditorContext& context)
		{
			if (context.GetDocumentPath())
				return *context.GetDocumentPath();
			return context.GetDocumentKind() == EditorContext::DocumentKind::Scene ? "Untitled scene"
																				   : "Untitled prefab";
		}

		// A dialog's OK and Cancel buttons. Returns whether OK was pressed (or Enter); Cancel and Escape close it
		bool DrawDialogButtons(const char* okLabel, bool okEnabled = true)
		{
			ImGui::Spacing();
			ImGui::BeginDisabled(!okEnabled);
			const bool ok = ImGui::Button(okLabel, ImVec2(ImGui::GetFontSize() * 7.0f, 0.0f)) ||
				(okEnabled && ImGui::IsKeyPressed(ImGuiKey_Enter, false));
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(ImGui::GetFontSize() * 7.0f, 0.0f)) ||
				ImGui::IsKeyPressed(ImGuiKey_Escape, false))
				ImGui::CloseCurrentPopup();
			return ok;
		}

		void DrawModalError(const std::string& error)
		{
			if (!error.empty())
				ImGui::TextColored(ErrorColor, "%s", error.c_str());
		}

	}

	EditorUI::EditorUI(EditorContext& context, EditorUIHost host)
		: m_Context(&context), m_Host(std::move(host))
	{
	}

	void EditorUI::ApplyStyle(float scale)
	{
		ImGui::StyleColorsDark();
		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowRounding = 4.0f;
		style.FrameRounding = 3.0f;
		style.GrabRounding = 3.0f;
		style.PopupRounding = 3.0f;
		style.TabRounding = 3.0f;
		style.WindowBorderSize = 1.0f;
		style.FramePadding = ImVec2(6.0f, 4.0f);
		style.ItemSpacing = ImVec2(8.0f, 5.0f);
		style.ScaleAllSizes(scale);
		style.FontScaleDpi = scale;
	}

	void EditorUI::Draw(float frameSeconds)
	{
		ImGuizmo::BeginFrame();
		HandleShortcuts();
		DrawMenuBar();
		DrawStatusBar();
		DrawDockSpace();

		if (m_ShowViewport)
			m_Viewport.Draw(*m_Context, m_Host.PrepareViewport, frameSeconds, &m_ShowViewport);
		if (m_ShowHierarchy)
			m_Hierarchy.Draw(*m_Context, &m_ShowHierarchy);
		if (m_ShowInspector)
			m_Inspector.Draw(*m_Context, &m_ShowInspector);
		if (m_ShowContentBrowser)
		{
			m_ContentBrowser.Draw(
				*m_Context, [this](std::string_view action, std::function<void()> run)
				{ ConfirmDiscard(action, std::move(run)); }, &m_ShowContentBrowser);
		}
		if (m_ShowConsole)
			m_Console.Draw(m_Context->GetLog(), &m_ShowConsole);

		DrawModals();
	}

	bool EditorUI::RequestQuit()
	{
		if (!m_Context->HasUnsavedChanges())
		{
			m_Quit = true;
			return true;
		}
		ConfirmDiscard("Quitting", [this] { m_Quit = true; });
		return false;
	}

	std::string EditorUI::GetWindowTitle() const
	{
		const std::string unsaved = m_Context->HasUnsavedChanges() ? "*" : "";
		if (const Project* project = m_Context->GetProject())
			return fmt::format(
				"Lodestone Editor - {} - {}{}", project->GetSettings().Name, GetDocumentName(*m_Context), unsaved);
		return fmt::format("Lodestone Editor - {}{}", GetDocumentName(*m_Context), unsaved);
	}

	void EditorUI::DrawDockSpace()
	{
		const ImGuiID dockSpace = ImHashStr(DockSpaceName);
		// The default layout the first time, before Dear ImGui has saved one, and when asked for
		if (m_ResetLayout || ImGui::DockBuilderGetNode(dockSpace) == nullptr)
		{
			BuildDefaultLayout(dockSpace);
			m_ShowHierarchy = m_ShowInspector = m_ShowViewport = m_ShowContentBrowser = m_ShowConsole = true;
			m_ResetLayout = false;
		}
		ImGui::DockSpaceOverViewport(dockSpace, ImGui::GetMainViewport());
	}

	void EditorUI::BuildDefaultLayout(ImGuiID dockSpace)
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::DockBuilderRemoveNode(dockSpace);
		ImGui::DockBuilderAddNode(dockSpace, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockSpace, viewport->WorkSize);

		ImGuiID center = dockSpace;
		const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.2f, nullptr, &center);
		const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.28f, nullptr, &center);
		const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.3f, nullptr, &center);
		ImGui::DockBuilderDockWindow(HierarchyPanel::Title, left);
		ImGui::DockBuilderDockWindow(InspectorPanel::Title, right);
		ImGui::DockBuilderDockWindow(ContentBrowserPanel::Title, bottom);
		ImGui::DockBuilderDockWindow(ConsolePanel::Title, bottom);
		ImGui::DockBuilderDockWindow(ViewportPanel::Title, center);
		ImGui::DockBuilderFinish(dockSpace);
	}

	void EditorUI::DrawMenuBar()
	{
		if (!ImGui::BeginMainMenuBar())
			return;
		const bool editing = IsEditing(*m_Context);
		const bool hasProject = m_Context->HasProject();
		const bool hasSelection = !m_Context->GetSelection().IsNil();

		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Project...", nullptr, false, editing))
				ConfirmDiscard("Creating a project", [this] { OpenModal(Modal::NewProject); });
			if (ImGui::MenuItem("Open Project...", nullptr, false, editing))
				ConfirmDiscard("Opening a project", [this] { OpenModal(Modal::OpenProject); });
			ImGui::Separator();
			if (ImGui::MenuItem("New Scene", "Ctrl+N", false, editing))
				NewScene();
			if (ImGui::MenuItem("Save", "Ctrl+S", false, editing && hasProject))
				Save();
			if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, editing && hasProject))
				OpenModal(Modal::SaveAs);
			ImGui::Separator();
			if (ImGui::MenuItem("Exit"))
				RequestQuit();
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Edit"))
		{
			const CommandHistory& history = m_Context->GetHistory();
			const std::string undo = fmt::format("Undo {}", history.GetUndoName().value_or(""));
			const std::string redo = fmt::format("Redo {}", history.GetRedoName().value_or(""));
			if (ImGui::MenuItem(undo.c_str(), "Ctrl+Z", false, editing && history.CanUndo()))
				Undo();
			if (ImGui::MenuItem(redo.c_str(), "Ctrl+Y", false, editing && history.CanRedo()))
				Redo();
			ImGui::Separator();
			if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, editing && hasSelection))
				DuplicateSelection();
			if (ImGui::MenuItem("Delete", "Delete", false, editing && hasSelection))
				DeleteSelection();
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Entity"))
		{
			if (ImGui::MenuItem("Create Entity", nullptr, false, editing))
				CreateEntity({});
			if (ImGui::MenuItem("Create Child Entity", nullptr, false, editing && hasSelection))
				CreateEntity(m_Context->GetSelection());
			ImGui::Separator();
			if (ImGui::MenuItem(
					"Create Prefab From Selection...", nullptr, false, editing && hasSelection && hasProject))
				OpenModal(Modal::CreatePrefab);
			if (ImGui::MenuItem("Frame Selection", "F", false, hasSelection))
				ViewportPanel::FrameSelection(*m_Context);
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Play"))
		{
			if (ImGui::MenuItem(editing ? "Play" : "Stop", "Ctrl+P"))
				TogglePlay();
			if (ImGui::MenuItem(
					m_Context->GetPlayState() == PlayState::Paused ? "Resume" : "Pause", nullptr, false, !editing))
				TogglePause();
			if (ImGui::MenuItem("Step", nullptr, false, m_Context->GetPlayState() == PlayState::Paused))
				Step();
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("View"))
		{
			ImGui::MenuItem(ViewportPanel::Title, nullptr, &m_ShowViewport);
			ImGui::MenuItem(HierarchyPanel::Title, nullptr, &m_ShowHierarchy);
			ImGui::MenuItem(InspectorPanel::Title, nullptr, &m_ShowInspector);
			ImGui::MenuItem(ContentBrowserPanel::Title, nullptr, &m_ShowContentBrowser);
			ImGui::MenuItem(ConsolePanel::Title, nullptr, &m_ShowConsole);
			ImGui::Separator();
			if (ImGui::MenuItem("Reset Layout"))
				m_ResetLayout = true;
			ImGui::EndMenu();
		}

		DrawPlayControls();
		ImGui::EndMainMenuBar();
	}

	void EditorUI::DrawPlayControls()
	{
		const PlayState state = m_Context->GetPlayState();
		const char* playLabel = state == PlayState::Editing ? "Play" : "Stop";
		const char* pauseLabel = state == PlayState::Paused ? "Resume" : "Pause";
		const ImGuiStyle& style = ImGui::GetStyle();
		const float width = ImGui::CalcTextSize(playLabel).x + ImGui::CalcTextSize(pauseLabel).x +
			ImGui::CalcTextSize("Step").x + style.FramePadding.x * 6.0f + style.ItemSpacing.x * 2.0f;
		ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), (ImGui::GetWindowWidth() - width) * 0.5f));

		if (state != PlayState::Editing)
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.3f, 0.1f, 1.0f));
		if (ImGui::Button(playLabel))
			TogglePlay();
		if (state != PlayState::Editing)
			ImGui::PopStyleColor();
		ImGui::SetItemTooltip(
			"%s", state == PlayState::Editing ? "Play the scene (Ctrl+P)" : "Stop, restoring the scene (Ctrl+P)");
		ImGui::BeginDisabled(state == PlayState::Editing);
		if (ImGui::Button(pauseLabel))
			TogglePause();
		ImGui::EndDisabled();
		ImGui::BeginDisabled(state != PlayState::Paused);
		if (ImGui::Button("Step"))
			Step();
		ImGui::SetItemTooltip("Run one simulation tick");
		ImGui::EndDisabled();
	}

	void EditorUI::DrawStatusBar()
	{
		const ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
		if (ImGui::BeginViewportSideBar(
				"##StatusBar", ImGui::GetMainViewport(), ImGuiDir_Down, ImGui::GetFrameHeight(), flags))
		{
			if (ImGui::BeginMenuBar())
			{
				ImGui::TextUnformatted(GetDocumentName(*m_Context).c_str());
				if (m_Context->HasUnsavedChanges())
				{
					ImGui::SameLine(0.0f, 2.0f);
					ImGui::TextUnformatted("*");
				}
				ImGui::SameLine();
				ImGui::TextDisabled("|");
				ImGui::SameLine();
				const std::string_view state = ToString(m_Context->GetPlayState());
				ImGui::TextUnformatted(state.data(), state.data() + state.size());
				if (const Simulation* simulation = m_Context->GetSimulation())
				{
					ImGui::SameLine();
					ImGui::TextDisabled("tick %llu", static_cast<unsigned long long>(simulation->GetTick()));
				}

				const std::string mcp =
					m_Host.McpUrl.empty() ? std::string("MCP: stdio or off") : fmt::format("MCP: {}", m_Host.McpUrl);
				const float width = ImGui::CalcTextSize(mcp.c_str()).x;
				ImGui::SameLine(std::max(ImGui::GetCursorPosX(),
					ImGui::GetWindowWidth() - width - ImGui::GetStyle().WindowPadding.x * 2.0f));
				ImGui::TextDisabled("%s", mcp.c_str());
				ImGui::EndMenuBar();
			}
		}
		ImGui::End();
	}

	void EditorUI::HandleShortcuts()
	{
		constexpr ImGuiInputFlags global = ImGuiInputFlags_RouteGlobal;
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, global) && IsEditing(*m_Context))
			NewScene();
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, global) && IsEditing(*m_Context) && m_Context->HasProject())
			Save();
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, global) && IsEditing(*m_Context) &&
			m_Context->HasProject())
			OpenModal(Modal::SaveAs);
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, global | ImGuiInputFlags_Repeat))
			Undo();
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, global | ImGuiInputFlags_Repeat) ||
			ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, global | ImGuiInputFlags_Repeat))
			Redo();
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D, global))
			DuplicateSelection();
		if (ImGui::Shortcut(ImGuiKey_Delete, global))
			DeleteSelection();
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_P, global))
			TogglePlay();
	}

	void EditorUI::OpenModal(Modal modal)
	{
		m_RequestedModal = modal;
		m_ModalError.clear();
		switch (modal)
		{
			case Modal::NewProject:
				m_PathInput.clear();
				m_NameInput = "My Game";
				break;
			case Modal::OpenProject:
				m_PathInput.clear();
				break;
			case Modal::SaveAs:
				m_PathInput = m_Context->GetDocumentPath().value_or(
					m_Context->GetDocumentKind() == EditorContext::DocumentKind::Scene ? "Scenes/Untitled.lscene"
																					   : "Prefabs/Untitled.lprefab");
				break;
			case Modal::CreatePrefab:
				m_PathInput =
					fmt::format("Prefabs/{}.lprefab", GetEntityLabel(m_Context->GetScene(), m_Context->GetSelection()));
				break;
			case Modal::ConfirmDiscard:
			case Modal::None:
				break;
		}
	}

	void EditorUI::DrawModals()
	{
		if (m_RequestedModal != Modal::None)
		{
			m_OpenModal = m_RequestedModal;
			m_RequestedModal = Modal::None;
			switch (m_OpenModal)
			{
				case Modal::NewProject:
					ImGui::OpenPopup(NewProjectTitle);
					break;
				case Modal::OpenProject:
					ImGui::OpenPopup(OpenProjectTitle);
					break;
				case Modal::SaveAs:
					ImGui::OpenPopup(SaveAsTitle);
					break;
				case Modal::CreatePrefab:
					ImGui::OpenPopup(CreatePrefabTitle);
					break;
				case Modal::ConfirmDiscard:
					ImGui::OpenPopup(ConfirmDiscardTitle);
					break;
				case Modal::None:
					break;
			}
		}

		const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		switch (m_OpenModal)
		{
			case Modal::NewProject:
				DrawNewProjectModal();
				break;
			case Modal::OpenProject:
				DrawOpenProjectModal();
				break;
			case Modal::SaveAs:
				DrawSaveAsModal();
				break;
			case Modal::CreatePrefab:
				DrawCreatePrefabModal();
				break;
			case Modal::ConfirmDiscard:
				DrawConfirmDiscardModal();
				break;
			case Modal::None:
				break;
		}
	}

	void EditorUI::DrawNewProjectModal()
	{
		if (!ImGui::BeginPopupModal(NewProjectTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			m_OpenModal = Modal::None;
			return;
		}
		ImGui::TextUnformatted("The project's directory, which must be empty or not exist yet");
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 30.0f);
		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		ImGui::InputTextWithHint("##Directory", "C:/Games/MyGame", &m_PathInput);
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 30.0f);
		ImGui::InputText("Name", &m_NameInput);
		DrawModalError(m_ModalError);
		if (DrawDialogButtons("Create", !m_PathInput.empty() && !m_NameInput.empty()))
		{
			if (auto created = m_Context->CreateProject(PathFromUtf8(m_PathInput), m_NameInput); created)
				ImGui::CloseCurrentPopup();
			else
				m_ModalError = created.error().ToString();
		}
		ImGui::EndPopup();
	}

	void EditorUI::DrawOpenProjectModal()
	{
		if (!ImGui::BeginPopupModal(OpenProjectTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			m_OpenModal = Modal::None;
			return;
		}
		ImGui::Text("The directory with the project's %.*s", static_cast<int>(Project::FileName.size()),
			Project::FileName.data());
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 30.0f);
		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		ImGui::InputTextWithHint("##Directory", "C:/Games/MyGame", &m_PathInput);
		DrawModalError(m_ModalError);
		if (DrawDialogButtons("Open", !m_PathInput.empty()))
		{
			if (auto opened = m_Context->OpenProject(PathFromUtf8(m_PathInput)); opened)
				ImGui::CloseCurrentPopup();
			else
				m_ModalError = opened.error().ToString();
		}
		ImGui::EndPopup();
	}

	void EditorUI::DrawSaveAsModal()
	{
		if (!ImGui::BeginPopupModal(SaveAsTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			m_OpenModal = Modal::None;
			return;
		}
		const bool scene = m_Context->GetDocumentKind() == EditorContext::DocumentKind::Scene;
		ImGui::Text("Where to save the %s, relative to the project's Assets directory", scene ? "scene" : "prefab");
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 30.0f);
		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		ImGui::InputText("##Path", &m_PathInput);
		DrawModalError(m_ModalError);
		if (DrawDialogButtons("Save", !m_PathInput.empty()))
		{
			if (auto saved = m_Context->SaveDocument(m_PathInput); saved)
			{
				LS_CORE_INFO("Saved {}", m_PathInput);
				ImGui::CloseCurrentPopup();
			}
			else
			{
				m_ModalError = saved.error().ToString();
			}
		}
		ImGui::EndPopup();
	}

	void EditorUI::DrawCreatePrefabModal()
	{
		if (!ImGui::BeginPopupModal(CreatePrefabTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			m_OpenModal = Modal::None;
			return;
		}
		const UUID selection = m_Context->GetSelection();
		ImGui::Text("Saves %s and its children as a prefab, relative to the project's Assets directory",
			GetEntityLabel(m_Context->GetScene(), selection).c_str());
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 30.0f);
		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		ImGui::InputText("##Path", &m_PathInput);
		DrawModalError(m_ModalError);
		if (DrawDialogButtons("Create", !m_PathInput.empty() && !selection.IsNil()))
		{
			if (auto saved = m_Context->SavePrefab(selection, m_PathInput); saved)
			{
				LS_CORE_INFO("Created the prefab {}", m_PathInput);
				ImGui::CloseCurrentPopup();
			}
			else
			{
				m_ModalError = saved.error().ToString();
			}
		}
		ImGui::EndPopup();
	}

	void EditorUI::DrawConfirmDiscardModal()
	{
		if (!ImGui::BeginPopupModal(ConfirmDiscardTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			m_OpenModal = Modal::None;
			return;
		}
		ImGui::Text(
			"%s has unsaved changes. %s will lose them.", GetDocumentName(*m_Context).c_str(), m_DiscardAction.c_str());
		DrawModalError(m_ModalError);
		ImGui::Spacing();

		const ImVec2 buttonSize(ImGui::GetFontSize() * 8.0f, 0.0f);
		std::function<void()> run;
		ImGui::BeginDisabled(!m_Context->GetDocumentPath() || !IsEditing(*m_Context));
		if (ImGui::Button("Save", buttonSize))
		{
			if (auto saved = m_Context->SaveDocument(); saved)
				run = std::exchange(m_AfterDiscard, {});
			else
				m_ModalError = saved.error().ToString();
		}
		ImGui::EndDisabled();
		if (!m_Context->GetDocumentPath())
			ImGui::SetItemTooltip("The document hasn't been saved yet: cancel, and use File > Save As");
		ImGui::SameLine();
		if (ImGui::Button("Don't Save", buttonSize))
			run = std::exchange(m_AfterDiscard, {});
		ImGui::SameLine();
		if (ImGui::Button("Cancel", buttonSize) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		{
			m_AfterDiscard = {};
			ImGui::CloseCurrentPopup();
		}
		if (run)
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		// After the popup ends, since the action may open another
		if (run)
			run();
	}

	void EditorUI::ConfirmDiscard(std::string_view action, std::function<void()> run)
	{
		if (!m_Context->HasUnsavedChanges())
		{
			run();
			return;
		}
		m_DiscardAction = std::string(action);
		m_AfterDiscard = std::move(run);
		OpenModal(Modal::ConfirmDiscard);
	}

	void EditorUI::NewScene()
	{
		ConfirmDiscard("Creating a scene",
			[this]
			{
				if (auto created = m_Context->NewScene(); !created)
					ReportFailure("Creating a scene", created.error());
			});
	}

	void EditorUI::Save()
	{
		const std::optional<std::string>& documentPath = m_Context->GetDocumentPath();
		if (!documentPath)
		{
			OpenModal(Modal::SaveAs);
			return;
		}
		const std::string path = *documentPath;
		if (auto saved = m_Context->SaveDocument(); saved)
			LS_CORE_INFO("Saved {}", path);
		else
			ReportFailure("Saving", saved.error());
	}

	void EditorUI::Undo()
	{
		if (!IsEditing(*m_Context) || !m_Context->GetHistory().CanUndo())
			return;
		if (auto undone = m_Context->Undo(); !undone)
			ReportFailure("Undo", undone.error());
	}

	void EditorUI::Redo()
	{
		if (!IsEditing(*m_Context) || !m_Context->GetHistory().CanRedo())
			return;
		if (auto redone = m_Context->Redo(); !redone)
			ReportFailure("Redo", redone.error());
	}

	void EditorUI::CreateEntity(UUID parent)
	{
		auto command = CreateScope<CreateEntityCommand>("Entity", parent);
		const CreateEntityCommand& created = *command;
		if (RunCommand(*m_Context, std::move(command)))
			m_Context->Select(created.GetEntityId());
	}

	void EditorUI::DuplicateSelection()
	{
		const UUID selection = m_Context->GetSelection();
		if (selection.IsNil() || !IsEditing(*m_Context))
			return;
		auto command = CreateScope<DuplicateEntityCommand>(selection);
		const DuplicateEntityCommand& duplicate = *command;
		if (RunCommand(*m_Context, std::move(command)))
			m_Context->Select(duplicate.GetCopyId());
	}

	void EditorUI::DeleteSelection()
	{
		const UUID selection = m_Context->GetSelection();
		if (selection.IsNil() || !IsEditing(*m_Context))
			return;
		RunCommand(*m_Context, CreateScope<DestroyEntityCommand>(selection));
	}

	void EditorUI::TogglePlay()
	{
		auto toggled = IsEditing(*m_Context) ? m_Context->StartPlay() : m_Context->StopPlay();
		if (!toggled)
			ReportFailure(IsEditing(*m_Context) ? "Starting play mode" : "Stopping play mode", toggled.error());
	}

	void EditorUI::TogglePause()
	{
		if (auto paused = m_Context->SetPaused(m_Context->GetPlayState() == PlayState::Playing); !paused)
			ReportFailure("Pausing", paused.error());
	}

	void EditorUI::Step()
	{
		if (auto stepped = m_Context->Step(1); !stepped)
			ReportFailure("Stepping", stepped.error());
	}

}
