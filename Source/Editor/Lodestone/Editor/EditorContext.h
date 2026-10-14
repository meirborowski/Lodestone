#pragma once

#include "Lodestone/Animation/ScenePose.h"
#include "Lodestone/Asset/AssetRegistry.h"
#include "Lodestone/Asset/Model.h"
#include "Lodestone/Asset/ModelCache.h"
#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/Error.h"
#include "Lodestone/Core/UUID.h"
#include "Lodestone/Editor/Commands/CommandHistory.h"
#include "Lodestone/Editor/EditorCamera.h"
#include "Lodestone/Editor/LogBuffer.h"
#include "Lodestone/Input/InputCommand.h"
#include "Lodestone/Physics/PhysicsWorld.h"
#include "Lodestone/Project/Project.h"
#include "Lodestone/Scene/Scene.h"
#include "Lodestone/Scripting/ScriptAssets.h"
#include "Lodestone/Scripting/ScriptSystem.h"
#include "Lodestone/Simulation/Simulation.h"

#include <glm/vec3.hpp>

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

		EditorContext(const EditorContext&) = delete;
		EditorContext& operator=(const EditorContext&) = delete;
		EditorContext(EditorContext&&) = delete;
		EditorContext& operator=(EditorContext&&) = delete;
		~EditorContext() = default;

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
		// Finds new, changed and removed assets (see AssetRegistry::Scan()). Fails without a project
		[[nodiscard]] std::expected<AssetScanReport, Error> ScanAssets();
		// Changes whenever the project's assets may have: when a project opens, and with every scan - so whatever
		// keeps assets loaded knows to load them again
		uint64_t GetAssetGeneration() const { return m_AssetGeneration; }

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
		// Instances a model asset (.gltf or .glb) into the document, as a command: an entity for each of the model's
		// nodes, drawing its meshes, under a root named after the model (see BuildModelEntities()), under a parent
		// (nil for a root entity). Returns the root
		[[nodiscard]] std::expected<UUID, Error> InstantiateModel(
			std::string_view path, UUID parent = {}, std::optional<size_t> index = std::nullopt);
		// Instances a prefab or a model, whichever the asset is
		[[nodiscard]] std::expected<UUID, Error> InstantiateAsset(
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
		// A system that every play session's simulation runs, after the ones added before it and before scripts -
		// how gameplay modules join play mode
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

		// Animation (see docs/Decisions/0020-skeletal-animation.md). The models animators play are imported from the
		// project's assets on first use, without their textures, and kept until they change - whatever the renderer
		// has loaded, so play mode simulates the same with or without one
		[[nodiscard]] std::expected<const Model*, Error> LoadModel(UUID id);
		// LoadModel() as a lookup: what play mode's animation system, the preview and the timeline use
		const ModelLookup& GetModelLookup() const { return m_ModelLookup; }
		// Models from somewhere else than the project's assets, for tests; an empty lookup goes back to the assets
		void SetModelLookup(ModelLookup models) { m_ModelOverride = std::move(models); }

		struct AnimationPreview
		{
			AnimatorPreview Pose;
			// Whether the preview's time advances with real time, at the animator's speed
			bool Playing = false;
		};
		// Draws an animator's entity with a clip at a time, in place of the animator's state, without changing the
		// document - the timeline's preview. Only while editing; fails for entities without an Animator and clips the
		// model doesn't have
		[[nodiscard]] std::expected<void, Error> SetAnimationPreview(
			UUID entity, uint32_t clip, float time, bool playing = false);
		void StopAnimationPreview() { m_AnimationPreview.reset(); }
		const std::optional<AnimationPreview>& GetAnimationPreview() const { return m_AnimationPreview; }
		// How the scene is drawn now: between ticks while playing, and with the preview while editing
		PoseOptions GetPoseOptions() const;

		// Physics (see docs/Decisions/0021-physics.md). Play mode runs a physics world for the scene, after the play
		// systems and before animation
		static constexpr size_t MaxRecentPhysicsEvents = 256;
		struct RecentPhysicsEvent
		{
			uint64_t Tick = 0;
			PhysicsEvent Event;
		};
		// The world of the running play session, or nullptr while editing
		const PhysicsWorld* GetPhysicsWorld() const { return m_Physics.get(); }
		// The latest collision and trigger events of the play session, oldest first: kept after it stops, until the
		// next one starts
		const std::deque<RecentPhysicsEvent>& GetRecentPhysicsEvents() const { return m_RecentPhysicsEvents; }
		// Casts a ray into the running physics world, or while editing into one built from the document (see
		// PhysicsWorld::Raycast())
		std::optional<RaycastHit> Raycast(
			const glm::vec3& origin, const glm::vec3& direction, const RaycastOptions& options = {});
		std::vector<RaycastHit> RaycastAll(
			const glm::vec3& origin, const glm::vec3& direction, const RaycastOptions& options = {});

		// Scripting (see docs/Decisions/0022-scripting.md). Play mode runs the scene's scripts after the play systems
		// and before physics, and gives them physics' events after it. The play session's scripts, or nullptr while
		// editing
		const ScriptSystem* GetScripts() const { return m_Scripts.get(); }
		// Loads the play session's scripts again (see ScriptSystem::Reload()); does nothing while editing
		void ReloadScripts();
		// ReloadScripts() if any of their files changed; whether they had. Update() checks every so often while
		// playing or paused
		bool ReloadChangedScripts();

		LogBuffer& GetLog() { return m_Log; }

	private:
		void SetProject(Project project);
		void ReplaceScene(Scope<Scene> scene, DocumentKind kind, std::optional<std::string> path);
		[[nodiscard]] std::expected<void, Error> RequireEditing() const;
		std::vector<InputCommand> SampleInput(uint64_t tick, const Simulation::InputSampler& live);
		void RescanAssets();
		void UpdateAnimationPreview(double elapsedSeconds);
		// A physics world of the document as it is, for queries while editing
		Scope<PhysicsWorld> CreateQueryWorld();

	private:
		const ComponentRegistry* m_Components;
		Scope<Project> m_Project;
		Scope<AssetRegistry> m_Assets;
		Scope<ProjectScriptAssets> m_ScriptAssets;
		uint64_t m_AssetGeneration = 0;

		Scope<Scene> m_Scene;
		DocumentKind m_DocumentKind = DocumentKind::Scene;
		std::optional<std::string> m_DocumentPath;
		CommandHistory m_History;
		uint64_t m_SavedStateId = 0;
		UUID m_Selection;
		EditorCamera m_Camera;

		PlayState m_PlayState = PlayState::Editing;
		std::vector<std::pair<std::string, SystemFunction>> m_PlaySystems;
		// Before the simulation, which runs them, so they're destroyed after it - and physics before the scripts,
		// which use it
		Scope<PhysicsWorld> m_Physics;
		Scope<ScriptSystem> m_Scripts;
		double m_SinceScriptCheck = 0.0;
		std::deque<RecentPhysicsEvent> m_RecentPhysicsEvents;
		Scope<Simulation> m_Simulation;
		SceneSnapshot m_EditSnapshot;
		std::deque<InputCommand> m_QueuedInput;

		Scope<ModelCache> m_ModelCache;
		ModelLookup m_ModelOverride;
		// Calls LoadModel(), so the context can't be copied or moved
		ModelLookup m_ModelLookup;
		std::optional<AnimationPreview> m_AnimationPreview;

		LogBuffer m_Log;
	};

}
