#pragma once

#include "Lodestone/Core/UUID.h"
#include "Lodestone/Editor/EditorContext.h"
#include "Lodestone/Editor/UI/ConsolePanel.h"
#include "Lodestone/Editor/UI/ContentBrowserPanel.h"
#include "Lodestone/Editor/UI/HierarchyPanel.h"
#include "Lodestone/Editor/UI/InspectorPanel.h"
#include "Lodestone/Editor/UI/ViewportPanel.h"

#include <imgui.h>

#include <functional>
#include <string>
#include <string_view>

namespace Lodestone {

	// What the editor UI needs from the application around it
	struct EditorUIHost
	{
		// Sizes the viewport's image and returns its texture, to show this frame
		ViewportPanel::PrepareImage PrepareViewport;
		// Where MCP clients reach the editor over HTTP, if it serves HTTP
		std::string McpUrl;
	};

	// The editor's docked panels - viewport, hierarchy, inspector, content browser and console - with the menus, play
	// controls, shortcuts and dialogs around them. Everything it changes goes through the editor context, as MCP's
	// changes do (see docs/Features/Editor.md)
	class EditorUI
	{
	public:
		EditorUI(EditorContext& context, EditorUIHost host);

		// Dear ImGui's style for the editor, at a scale for the display's DPI
		static void ApplyStyle(float scale);

		// Builds the frame's UI, between ImGui::NewFrame() and ImGui::Render()
		void Draw(float frameSeconds);

		// The user asked to close the editor. Returns whether it may close now: with unsaved changes, it asks what
		// to do with them first, and ShouldQuit() becomes true once they're dealt with
		bool RequestQuit();
		bool ShouldQuit() const { return m_Quit; }

		// Whether the viewport showed the scene this frame, so it needs rendering
		bool IsViewportVisible() const { return m_Viewport.IsVisible(); }
		// Whether the viewport has focus, so play mode takes the keyboard and mouse
		bool IsViewportFocused() const { return m_Viewport.IsFocused(); }

		// "Lodestone Editor - <project> - <document>", with a * for unsaved changes
		std::string GetWindowTitle() const;

	private:
		enum class Modal
		{
			None,
			NewProject,
			OpenProject,
			SaveAs,
			CreatePrefab,
			ConfirmDiscard,
		};

		void DrawDockSpace();
		static void BuildDefaultLayout(ImGuiID dockSpace);
		void DrawMenuBar();
		void DrawPlayControls();
		void DrawStatusBar();
		void HandleShortcuts();
		void DrawModals();
		void DrawNewProjectModal();
		void DrawOpenProjectModal();
		void DrawSaveAsModal();
		void DrawCreatePrefabModal();
		void DrawConfirmDiscardModal();
		void OpenModal(Modal modal);

		// Runs an action that replaces the document, asking first if that would lose unsaved changes
		void ConfirmDiscard(std::string_view action, std::function<void()> run);

		void NewScene();
		void Save();
		void Undo();
		void Redo();
		void CreateEntity(UUID parent);
		void DuplicateSelection();
		void DeleteSelection();
		void TogglePlay();
		void TogglePause();
		void Step();

	private:
		EditorContext* m_Context;
		EditorUIHost m_Host;

		HierarchyPanel m_Hierarchy;
		InspectorPanel m_Inspector;
		ViewportPanel m_Viewport;
		ContentBrowserPanel m_ContentBrowser;
		ConsolePanel m_Console;
		bool m_ShowHierarchy = true;
		bool m_ShowInspector = true;
		bool m_ShowViewport = true;
		bool m_ShowContentBrowser = true;
		bool m_ShowConsole = true;
		bool m_ResetLayout = false;
		bool m_Quit = false;

		// The dialog to open next frame, and the one that's open
		Modal m_RequestedModal = Modal::None;
		Modal m_OpenModal = Modal::None;
		std::string m_PathInput;
		std::string m_NameInput;
		std::string m_ModalError;
		std::string m_DiscardAction;
		std::function<void()> m_AfterDiscard;
	};

}
