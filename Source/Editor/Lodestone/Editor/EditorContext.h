#pragma once

#include "Lodestone/Asset/AssetRegistry.h"
#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/Error.h"
#include "Lodestone/Core/UUID.h"
#include "Lodestone/Editor/Commands/CommandHistory.h"
#include "Lodestone/Editor/EditorCamera.h"
#include "Lodestone/Editor/LogBuffer.h"
#include "Lodestone/Input/InputCommand.h"
#include "Lodestone/Project/Project.h"
#include "Lodestone/Scene/Scene.h"
#include "Lodestone/Simulation/Simulation.h"

#include <cstdint>
#include <deque>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Lodestone {

	enum class PlayState
	{
		// The scene is edited; nothing simulates
		Editing,
		// The simulation runs in real time, on the scene; stopping restores the scene as it was before playing
		Playing,
		// The simulation is paused; it can be stepped a tick at a time
		Paused,
	};

	std::string_view ToString(PlayState state);

	// What the editor works on - the project, the open document (a scene, or a prefab opened for editing), the
	// selection, the undo history and play mode - shared by the editor UI and the MCP tools, so both see and change the
	// same state, and both edit only through commands (see docs/Decisions/0015-editor-commands.md).
	//
	// Paths that tools give are relative to the project's asset directory and can't leave it. Used from the main
	// thread only
	class EditorContext
	{
	public:
		enum class DocumentKind
		{
			Scene,
			Prefab,
		};

		explicit EditorContext(const ComponentRegistry& components = GetEngineComponentRegistry());

		// Projects. Opening or creating a project starts a new, empty scene
		[[nodiscard]] std::expected<void, Error> CreateProject(
			const std::filesystem::path& directory, std::string_view name);
		[[nodiscard]] std::expected<void, Error> OpenProject(const std::filesystem::path& directory);
		bool HasProject() const { return m_Project != nullptr; }
		const Project* GetProject() const { return m_Project.get(); }
		AssetRegistry* GetAssets() { return m_Assets.get(); }
		const AssetRegistry* GetAssets() const { return m_Assets.get(); }
		// A path inside the asset directory, from a path relative to it. Fails without a project, and for paths that
		// would leave the asset directory
		[[nodiscard]] std::expected<std::filesystem::path, Error> ResolveAssetPath(std::string_view relativePath) const;

		// The open document
		Scene& GetScene() { return *m_Scene; }
		const Scene& GetScene() const { return *m_Scene; }
		DocumentKind GetDocumentKind() const { return m_DocumentKind; }
		// The document's path relative to the asset directory, if it's been saved
		const std::optional<std::string>& GetDocumentPath() const { return m_DocumentPath; }
		bool HasUnsavedChanges() const { return m_History.GetStateId() != m_SavedStateId; }

		[[nodiscard]] std::expected<void, Error> NewScene();
		[[nodiscard]] std::expected<void, Error> OpenScene(std::string_view path);
		// Opens a prefab to edit it as a document of its own: its root is the scene's only root entity
		[[nodiscard]] std::expected<void, Error> OpenPrefab(std::string_view path);
		// Saves the document, to its own path or a new one
		[[nodiscard]] std::expected<void, Error> SaveDocument(std::optional<std::string_view> path = std::nullopt);
		// Saves an entity and its descendants as a prefab asset
		[[nodiscard]] std::expected<void, Error> SavePrefab(UUID root, std::string_view path);
		// Instances a prefab asset into the document, as a command, under a parent (nil for a root entity). Returns
		// the instance's root
		[[nodiscard]] std::expected<UUID, Error> InstantiatePrefab(
			std::string_view path, UUID parent = {}, std::optional<size_t> index = std::nullopt);

		// Editing. Commands are rejected in play mode: what changes while playing is thrown away when it stops
		[[nodiscard]] std::expected<void, Error> Execute(Scope<Command> command);
		[[nodiscard]] std::expected<void, Error> Undo();
		[[nodiscard]] std::expected<void, Error> Redo();
		CommandHistory& GetHistory() { return m_History; }
		const CommandHistory& GetHistory() const { return m_History; }

		// The selected entity, or the nil UUID
		UUID GetSelection() const;
		void Select(UUID id) { m_Selection = id; }

		// The viewport's camera, which screenshots use too
		EditorCamera& GetCamera() { return m_Camera; }
		const EditorCamera& GetCamera() const { return m_Camera; }

		// Play mode
		PlayState GetPlayState() const { return m_PlayState; }
		// A system that every play session's simulation runs, after the ones added before it - how gameplay modules
		// such as physics and scripting join play mode
		void AddPlaySystem(std::string name, SystemFunction system);
		[[nodiscard]] std::expected<void, Error> StartPlay();
		[[nodiscard]] std::expected<void, Error> StopPlay();
		[[nodiscard]] std::expected<void, Error> SetPaused(bool paused);
		// Runs ticks right away, whether playing or paused
		[[nodiscard]] std::expected<void, Error> Step(uint32_t ticks);
		// Advances real time while playing. live, if given, provides the viewport's input for ticks without queued
		// input
		void Update(double elapsedSeconds, const Simulation::InputSampler& live = {});
		// Input for the coming ticks, one command per tick, for player 0, used before live input (MCP sends input
		// this way so agents can playtest)
		void QueueInput(std::span<const InputCommand> commands);
		size_t GetQueuedInputCount() const { return m_QueuedInput.size(); }
		const Simulation* GetSimulation() const { return m_Simulation.get(); }

		LogBuffer& GetLog() { return m_Log; }

	private:
		void ReplaceScene(Scope<Scene> scene, DocumentKind kind, std::optional<std::string> path);
		[[nodiscard]] std::expected<void, Error> RequireEditing() const;
		std::vector<InputCommand> SampleInput(uint64_t tick, const Simulation::InputSampler& live);
		void RescanAssets();

	private:
		const ComponentRegistry* m_Components;
		Scope<Project> m_Project;
		Scope<AssetRegistry> m_Assets;

		Scope<Scene> m_Scene;
		DocumentKind m_DocumentKind = DocumentKind::Scene;
		std::optional<std::string> m_DocumentPath;
		CommandHistory m_History;
		uint64_t m_SavedStateId = 0;
		UUID m_Selection;
		EditorCamera m_Camera;

		PlayState m_PlayState = PlayState::Editing;
		std::vector<std::pair<std::string, SystemFunction>> m_PlaySystems;
		Scope<Simulation> m_Simulation;
		SceneSnapshot m_EditSnapshot;
		std::deque<InputCommand> m_QueuedInput;

		LogBuffer m_Log;
	};

}
