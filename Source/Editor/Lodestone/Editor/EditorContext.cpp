#include "Lodestone/Editor/EditorContext.h"

#include "Lodestone/Animation/Animation.h"
#include "Lodestone/Animation/AnimationComponents.h"
#include "Lodestone/Asset/ModelImporter.h"
#include "Lodestone/Core/FileSystem.h"
#include "Lodestone/Editor/Commands/EntityCommands.h"
#include "Lodestone/Scene/Entity.h"
#include "Lodestone/Scene/EntitySerializer.h"
#include "Lodestone/Scene/ModelEntities.h"
#include "Lodestone/Scene/PrefabSerializer.h"
#include "Lodestone/Scene/SceneSerializer.h"
#include "Lodestone/Scripting/LuaAnnotations.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/std.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <system_error>
#include <utility>

namespace Lodestone {

	namespace {

		constexpr uint32_t DefaultTickRate = 60;
		// How often play mode looks for changed scripts, in seconds
		constexpr double ScriptCheckInterval = 0.5;

		std::expected<void, Error> RequireExtension(const std::filesystem::path& path, std::string_view extension)
		{
			if (path.extension() != extension)
				return std::unexpected(Error(ErrorCode::InvalidArgument,
					fmt::format("{} must have the {} extension", PathToUtf8(path.filename()), extension)));
			return {};
		}

		std::expected<void, Error> CreateParentDirectories(const std::filesystem::path& path)
		{
			std::error_code error;
			std::filesystem::create_directories(path.parent_path(), error);
			if (error)
				return std::unexpected(Error(
					ErrorCode::IoError, fmt::format("Can't create the directory {}: {}", path.parent_path(), error)));
			return {};
		}

	}

	std::string_view ToString(PlayState state)
	{
		switch (state)
		{
			case PlayState::Editing:
				return "Editing";
			case PlayState::Playing:
				return "Playing";
			case PlayState::Paused:
				return "Paused";
		}
		return "Unknown";
	}

	EditorContext::EditorContext(const ComponentRegistry& components)
		: m_Components(&components),
		  m_Scene(CreateScope<Scene>(components)),
		  m_SavedStateId(m_History.GetStateId()),
		  m_ModelLookup(
			  [this](UUID id) -> const Model*
			  {
				  auto model = LoadModel(id);
				  return model ? *model : nullptr;
			  })
	{
		m_Log.Attach();
	}

	std::expected<void, Error> EditorContext::CreateProject(
		const std::filesystem::path& directory, std::string_view name)
	{
		if (auto editing = RequireEditing(); !editing)
			return editing;
		auto project = Project::Create(directory, name);
		if (!project)
			return std::unexpected(project.error());
		SetProject(std::move(*project));
		return NewScene();
	}

	std::expected<void, Error> EditorContext::OpenProject(const std::filesystem::path& directory)
	{
		if (auto editing = RequireEditing(); !editing)
			return editing;
		auto project = Project::Open(directory);
		if (!project)
			return std::unexpected(project.error());
		SetProject(std::move(*project));
		return NewScene();
	}

	void EditorContext::SetProject(Project project)
	{
		m_Project = CreateScope<Project>(std::move(project));
		m_ScriptAssets.reset();
		m_ModelCache.reset();
		m_Assets = CreateScope<AssetRegistry>(m_Project->GetAssetDirectory());
		m_ModelCache = CreateScope<ModelCache>(*m_Assets);
		m_ScriptAssets = CreateScope<ProjectScriptAssets>(*m_Assets);
		RescanAssets();
		// So editors complete and check the project's scripts against this engine's API
		if (auto written = WriteLuaLanguageServerFiles(m_Project->GetDirectory()); !written)
			LS_CORE_WARN("The project's lua-language-server files couldn't be written: {}", written.error());
	}

	std::expected<std::filesystem::path, Error> EditorContext::ResolveAssetPath(std::string_view relativePath) const
	{
		if (!m_Project)
			return std::unexpected(Error(ErrorCode::InvalidState, "No project is open"));
		if (relativePath.empty())
			return std::unexpected(Error(ErrorCode::InvalidArgument, "The path is empty"));
		const std::filesystem::path relative = PathFromUtf8(relativePath);
		if (relative.has_root_name() || relative.has_root_directory())
			return std::unexpected(Error(ErrorCode::InvalidArgument,
				fmt::format("'{}' must be relative to the project's asset directory", relativePath)));

		const std::filesystem::path assets = m_Project->GetAssetDirectory().lexically_normal();
		std::filesystem::path resolved = (assets / relative).lexically_normal();
		const std::filesystem::path inside = resolved.lexically_relative(assets);
		if (inside.empty() || *inside.begin() == ".." || inside == ".")
			return std::unexpected(Error(ErrorCode::InvalidArgument,
				fmt::format("'{}' is outside the project's asset directory", relativePath)));
		return resolved;
	}

	std::expected<AssetScanReport, Error> EditorContext::ScanAssets()
	{
		if (!m_Assets)
			return std::unexpected(Error(ErrorCode::InvalidState, "No project is open"));
		++m_AssetGeneration;
		auto report = m_Assets->Scan();
		m_ModelCache->Refresh();
		return report;
	}

	std::expected<const Model*, Error> EditorContext::LoadModel(UUID id)
	{
		if (m_ModelOverride)
		{
			if (const Model* model = m_ModelOverride(id))
				return model;
			return std::unexpected(Error(ErrorCode::NotFound, fmt::format("There's no model with the ID {}", id)));
		}
		if (!m_ModelCache)
			return std::unexpected(Error(ErrorCode::InvalidState, "No project is open, so it has no models"));
		return m_ModelCache->Load(id);
	}

	std::expected<void, Error> EditorContext::NewScene()
	{
		if (auto editing = RequireEditing(); !editing)
			return editing;
		ReplaceScene(CreateScope<Scene>(*m_Components), DocumentKind::Scene, std::nullopt);
		return {};
	}

	std::expected<void, Error> EditorContext::OpenScene(std::string_view path)
	{
		if (auto editing = RequireEditing(); !editing)
			return editing;
		const auto resolved = ResolveAssetPath(path);
		if (!resolved)
			return std::unexpected(resolved.error());
		auto scene = SceneSerializer::Load(*resolved, *m_Components);
		if (!scene)
			return std::unexpected(scene.error());
		ReplaceScene(std::move(*scene), DocumentKind::Scene,
			PathToUtf8(resolved->lexically_relative(m_Project->GetAssetDirectory().lexically_normal())));
		return {};
	}

	std::expected<void, Error> EditorContext::OpenPrefab(std::string_view path)
	{
		if (auto editing = RequireEditing(); !editing)
			return editing;
		const auto resolved = ResolveAssetPath(path);
		if (!resolved)
			return std::unexpected(resolved.error());
		const auto entities = PrefabSerializer::Load(*resolved, *m_Components);
		if (!entities)
			return std::unexpected(entities.error());

		auto scene = CreateScope<Scene>(*m_Components);
		if (auto root = EntitySerializer::InstantiateTree(*scene, *entities, EntitySerializer::IdPolicy::Keep); !root)
			return std::unexpected(root.error());
		ReplaceScene(std::move(scene), DocumentKind::Prefab,
			PathToUtf8(resolved->lexically_relative(m_Project->GetAssetDirectory().lexically_normal())));
		return {};
	}

	std::expected<void, Error> EditorContext::SaveDocument(std::optional<std::string_view> path)
	{
		if (auto editing = RequireEditing(); !editing)
			return editing;
		if (!path && !m_DocumentPath)
			return std::unexpected(
				Error(ErrorCode::InvalidArgument, "The document hasn't been saved before, so it needs a path"));
		const auto resolved = ResolveAssetPath(path ? *path : *m_DocumentPath);
		if (!resolved)
			return std::unexpected(resolved.error());
		if (auto created = CreateParentDirectories(*resolved); !created)
			return created;

		if (m_DocumentKind == DocumentKind::Scene)
		{
			if (auto extension = RequireExtension(*resolved, ".lscene"); !extension)
				return extension;
			if (auto saved = SceneSerializer::Save(*m_Scene, *resolved); !saved)
				return saved;
		}
		else
		{
			if (auto extension = RequireExtension(*resolved, ".lprefab"); !extension)
				return extension;
			if (m_Scene->GetRootEntities().size() != 1)
				return std::unexpected(Error(ErrorCode::InvalidState,
					fmt::format("A prefab has exactly one root entity, but this one has {}",
						m_Scene->GetRootEntities().size())));
			const Entity root = m_Scene->FindEntity(m_Scene->GetRootEntities().front());
			if (auto saved = PrefabSerializer::Save(*m_Scene, root, *resolved); !saved)
				return saved;
		}

		m_DocumentPath = PathToUtf8(resolved->lexically_relative(m_Project->GetAssetDirectory().lexically_normal()));
		m_SavedStateId = m_History.GetStateId();
		RescanAssets();
		return {};
	}

	std::expected<void, Error> EditorContext::SavePrefab(UUID root, std::string_view path)
	{
		if (auto editing = RequireEditing(); !editing)
			return editing;
		const Entity entity = m_Scene->FindEntity(root);
		if (!entity)
			return std::unexpected(Error(ErrorCode::NotFound, fmt::format("The scene has no entity {}", root)));
		const auto resolved = ResolveAssetPath(path);
		if (!resolved)
			return std::unexpected(resolved.error());
		if (auto extension = RequireExtension(*resolved, ".lprefab"); !extension)
			return extension;
		if (auto created = CreateParentDirectories(*resolved); !created)
			return created;
		if (auto saved = PrefabSerializer::Save(*m_Scene, entity, *resolved); !saved)
			return saved;
		RescanAssets();
		return {};
	}

	std::expected<UUID, Error> EditorContext::InstantiatePrefab(
		std::string_view path, UUID parent, std::optional<size_t> index)
	{
		if (auto editing = RequireEditing(); !editing)
			return std::unexpected(editing.error());
		const auto resolved = ResolveAssetPath(path);
		if (!resolved)
			return std::unexpected(resolved.error());
		if (auto extension = RequireExtension(*resolved, ".lprefab"); !extension)
			return std::unexpected(extension.error());
		auto prefab = PrefabSerializer::Load(*resolved, *m_Components);
		if (!prefab)
			return std::unexpected(prefab.error());

		const AssetInfo* asset =
			m_Assets->FindByPath(resolved->lexically_relative(m_Project->GetAssetDirectory().lexically_normal()));
		auto command = CreateScope<InstantiatePrefabCommand>(std::move(*prefab),
			asset != nullptr ? asset->Metadata.Id : UUID(), PathToUtf8(resolved->stem()), parent, index);
		const InstantiatePrefabCommand& instance = *command;
		if (auto executed = Execute(std::move(command)); !executed)
			return std::unexpected(executed.error());
		// The history owns the command now
		return instance.GetInstanceId();
	}

	std::expected<UUID, Error> EditorContext::InstantiateModel(
		std::string_view path, UUID parent, std::optional<size_t> index)
	{
		if (auto editing = RequireEditing(); !editing)
			return std::unexpected(editing.error());
		const auto resolved = ResolveAssetPath(path);
		if (!resolved)
			return std::unexpected(resolved.error());
		if (GetAssetTypeForFile(*resolved) != AssetType::Model)
			return std::unexpected(Error(ErrorCode::InvalidArgument,
				fmt::format("{} isn't a model: models are .gltf or .glb files", PathToUtf8(resolved->filename()))));

		// The meshes refer to the model by its asset UUID, so it must be registered
		const std::filesystem::path relative =
			resolved->lexically_relative(m_Project->GetAssetDirectory().lexically_normal());
		const AssetInfo* asset = m_Assets->FindByPath(relative);
		if (asset == nullptr && std::filesystem::is_regular_file(*resolved))
		{
			RescanAssets();
			asset = m_Assets->FindByPath(relative);
		}
		if (asset == nullptr)
			return std::unexpected(Error(ErrorCode::NotFound, fmt::format("The project has no model '{}'", path)));
		// Only the nodes and meshes are needed: the renderer loads the textures when it draws the model
		auto model = ModelImporter::Import(*resolved, {.LoadTextures = false});
		if (!model)
			return std::unexpected(model.error());

		const std::string name = PathToUtf8(resolved->stem());
		auto command = CreateScope<InstantiateModelCommand>(
			BuildModelEntities(*model, asset->Metadata.Id, name, *m_Components), name, parent, index);
		const InstantiateModelCommand& instance = *command;
		if (auto executed = Execute(std::move(command)); !executed)
			return std::unexpected(executed.error());
		// The history owns the command now
		return instance.GetInstanceId();
	}

	std::expected<UUID, Error> EditorContext::InstantiateAsset(
		std::string_view path, UUID parent, std::optional<size_t> index)
	{
		if (GetAssetTypeForFile(PathFromUtf8(path)) == AssetType::Model)
			return InstantiateModel(path, parent, index);
		return InstantiatePrefab(path, parent, index);
	}

	std::expected<void, Error> EditorContext::Execute(Scope<Command> command)
	{
		if (auto editing = RequireEditing(); !editing)
			return editing;
		return m_History.Execute(*m_Scene, std::move(command));
	}

	std::expected<void, Error> EditorContext::Undo()
	{
		if (auto editing = RequireEditing(); !editing)
			return editing;
		return m_History.Undo(*m_Scene);
	}

	std::expected<void, Error> EditorContext::Redo()
	{
		if (auto editing = RequireEditing(); !editing)
			return editing;
		return m_History.Redo(*m_Scene);
	}

	UUID EditorContext::GetSelection() const
	{
		return m_Scene->Contains(m_Selection) ? m_Selection : UUID();
	}

	std::expected<void, Error> EditorContext::StartPlay()
	{
		if (m_PlayState != PlayState::Editing)
			return std::unexpected(Error(ErrorCode::InvalidState, "Play mode is already running"));
		m_History.EndMerge();
		m_EditSnapshot = m_Scene->SaveSnapshot();
		m_AnimationPreview.reset();
		const uint32_t tickRate = m_Project ? m_Project->GetSettings().TickRate : DefaultTickRate;
		m_Simulation = CreateScope<Simulation>(*m_Scene, SimulationConfig{.TickRate = tickRate});
		m_Physics = CreateScope<PhysicsWorld>(*m_Scene, PhysicsWorldConfig{.Models = m_ModelLookup});
		// In line with the scene from the start, so the first tick's scripts can query it
		m_Physics->Sync();
		m_Scripts = CreateScope<ScriptSystem>(*m_Scene,
			ScriptSystemConfig{.Assets = m_ScriptAssets.get(), .Models = m_ModelLookup, .Physics = m_Physics.get()});
		m_SinceScriptCheck = 0.0;
		m_RecentPhysicsEvents.clear();
		for (const auto& [name, system] : m_PlaySystems)
			m_Simulation->AddSystem(name, system);
		m_Simulation->AddSystem("Scripts", [this](SimulationContext& simulation) { m_Scripts->Update(simulation); });
		// Physics steps after gameplay, which may have moved bodies this tick
		m_Simulation->AddSystem("Physics", *m_Physics);
		m_Simulation->AddSystem("PhysicsEvents",
			[this](SimulationContext& simulation)
			{
				m_Scripts->DeliverEvents(simulation, m_Physics->GetEvents());
				for (const PhysicsEvent& event : m_Physics->GetEvents())
				{
					if (m_RecentPhysicsEvents.size() == MaxRecentPhysicsEvents)
						m_RecentPhysicsEvents.pop_front();
					m_RecentPhysicsEvents.push_back({.Tick = simulation.GetTick(), .Event = event});
				}
			});
		// Animators advance after gameplay, which may have started clips this tick
		m_Simulation->AddSystem("Animation", [this](SimulationContext& simulation)
			{ AdvanceAnimators(simulation.GetScene(), simulation.GetDeltaTime(), m_ModelLookup); });
		m_PlayState = PlayState::Playing;
		LS_CORE_INFO("Play mode started");
		return {};
	}

	void EditorContext::AddPlaySystem(std::string name, SystemFunction system)
	{
		m_PlaySystems.emplace_back(std::move(name), std::move(system));
	}

	std::expected<void, Error> EditorContext::StopPlay()
	{
		if (m_PlayState == PlayState::Editing)
			return std::unexpected(Error(ErrorCode::InvalidState, "Play mode isn't running"));
		// Scripts end on the play scene, before it's restored
		m_Scripts->Shutdown();
		m_Simulation.reset();
		m_Scripts.reset();
		m_Physics.reset();
		m_Scene->RestoreSnapshot(m_EditSnapshot);
		m_EditSnapshot = SceneSnapshot();
		m_QueuedInput.clear();
		m_PlayState = PlayState::Editing;
		LS_CORE_INFO("Play mode stopped; the scene is back as it was");
		return {};
	}

	std::expected<void, Error> EditorContext::SetPaused(bool paused)
	{
		if (m_PlayState == PlayState::Editing)
			return std::unexpected(Error(ErrorCode::InvalidState, "Play mode isn't running"));
		m_PlayState = paused ? PlayState::Paused : PlayState::Playing;
		return {};
	}

	std::expected<void, Error> EditorContext::Step(uint32_t ticks)
	{
		if (m_PlayState == PlayState::Editing)
			return std::unexpected(Error(ErrorCode::InvalidState, "Play mode isn't running"));
		for (uint32_t i = 0; i < ticks; ++i)
			m_Simulation->Step(SampleInput(m_Simulation->GetTick(), {}));
		return {};
	}

	void EditorContext::Update(double elapsedSeconds, const Simulation::InputSampler& live)
	{
		if (m_PlayState == PlayState::Editing)
		{
			UpdateAnimationPreview(elapsedSeconds);
			return;
		}
		m_SinceScriptCheck += elapsedSeconds;
		if (m_SinceScriptCheck >= ScriptCheckInterval)
		{
			m_SinceScriptCheck = 0.0;
			ReloadChangedScripts();
		}
		if (m_PlayState != PlayState::Playing)
			return;
		m_Simulation->Advance(elapsedSeconds, [this, &live](uint64_t tick) { return SampleInput(tick, live); });
	}

	void EditorContext::ReloadScripts()
	{
		if (m_Scripts)
			m_Scripts->Reload();
	}

	bool EditorContext::ReloadChangedScripts()
	{
		return m_Scripts && m_Scripts->ReloadChanged();
	}

	std::expected<void, Error> EditorContext::SetAnimationPreview(UUID entity, uint32_t clip, float time, bool playing)
	{
		if (auto editing = RequireEditing(); !editing)
			return std::unexpected(editing.error());
		const Entity found = m_Scene->FindEntity(entity);
		if (!found.IsValid())
			return std::unexpected(Error(ErrorCode::NotFound, fmt::format("No entity has the ID {}", entity)));
		if (found.TryGet<AnimatorComponent>() == nullptr)
			return std::unexpected(Error(
				ErrorCode::InvalidArgument, fmt::format("Entity '{}' has no Animator to preview", found.GetName())));
		if (!std::isfinite(time))
			return std::unexpected(Error(ErrorCode::InvalidArgument, "The preview's time isn't a number"));
		const auto* instance = found.TryGet<ModelInstanceComponent>();
		if (const Model* model = instance != nullptr ? m_ModelLookup(instance->Model) : nullptr;
			model != nullptr && clip >= model->Animations.size())
			return std::unexpected(Error(ErrorCode::InvalidArgument,
				fmt::format("The model of '{}' has {} clips, so it has no clip {}", found.GetName(),
					model->Animations.size(), clip)));
		m_AnimationPreview = AnimationPreview{
			.Pose = {.Entity = entity, .Clip = clip, .Time = time},
			.Playing = playing,
		};
		return {};
	}

	std::optional<RaycastHit> EditorContext::Raycast(
		const glm::vec3& origin, const glm::vec3& direction, const RaycastOptions& options)
	{
		if (m_Physics)
			return m_Physics->Raycast(origin, direction, options);
		return CreateQueryWorld()->Raycast(origin, direction, options);
	}

	std::vector<RaycastHit> EditorContext::RaycastAll(
		const glm::vec3& origin, const glm::vec3& direction, const RaycastOptions& options)
	{
		if (m_Physics)
			return m_Physics->RaycastAll(origin, direction, options);
		return CreateQueryWorld()->RaycastAll(origin, direction, options);
	}

	Scope<PhysicsWorld> EditorContext::CreateQueryWorld()
	{
		// Sized for the document - a body at most for each entity - and never stepped, so it needs no threads and
		// little scratch memory
		const auto bodies = static_cast<uint32_t>(std::min<size_t>(m_Scene->GetEntityCount() + 1, 65536));
		auto world = CreateScope<PhysicsWorld>(*m_Scene,
			PhysicsWorldConfig{
				.Models = m_ModelLookup,
				.MaxBodies = bodies,
				.MaxBodyPairs = 1024,
				.MaxContactConstraints = 1024,
				.WorkerThreads = 0,
				.TempMemory = 64u * 1024,
			});
		world->Sync();
		return world;
	}

	PoseOptions EditorContext::GetPoseOptions() const
	{
		PoseOptions options{.Models = m_ModelLookup};
		if (m_Simulation)
		{
			options.InterpolationFactor = m_Simulation->GetInterpolationFactor();
			options.TickDuration = static_cast<float>(m_Simulation->GetTickDuration());
		}
		if (m_PlayState == PlayState::Editing && m_AnimationPreview)
			options.Preview = m_AnimationPreview->Pose;
		return options;
	}

	void EditorContext::UpdateAnimationPreview(double elapsedSeconds)
	{
		if (!m_AnimationPreview || !m_AnimationPreview->Playing)
			return;
		AnimatorPreview& preview = m_AnimationPreview->Pose;
		const Entity entity = m_Scene->FindEntity(preview.Entity);
		const auto* animator = entity.IsValid() ? entity.TryGet<AnimatorComponent>() : nullptr;
		if (animator == nullptr)
		{
			// The entity, or its animator, is gone
			m_AnimationPreview.reset();
			return;
		}
		preview.Time += static_cast<float>(elapsedSeconds) * animator->Speed;
		const auto* instance = entity.TryGet<ModelInstanceComponent>();
		const Model* model = instance != nullptr ? m_ModelLookup(instance->Model) : nullptr;
		if (model == nullptr || preview.Clip >= model->Animations.size())
			return;
		const AnimationClip& clip = model->Animations[preview.Clip];
		// Like the animator: around again if it loops, stopping at the end if it doesn't
		const float time = GetClipTime(clip, preview.Time, animator->Loop);
		if (!animator->Loop && time != preview.Time)
			m_AnimationPreview->Playing = false;
		preview.Time = time;
	}

	void EditorContext::QueueInput(std::span<const InputCommand> commands)
	{
		m_QueuedInput.insert(m_QueuedInput.end(), commands.begin(), commands.end());
	}

	void EditorContext::ReplaceScene(Scope<Scene> scene, DocumentKind kind, std::optional<std::string> path)
	{
		m_Scene = std::move(scene);
		m_DocumentKind = kind;
		m_DocumentPath = std::move(path);
		m_History.Clear();
		m_SavedStateId = m_History.GetStateId();
		m_Selection = UUID();
		m_AnimationPreview.reset();
	}

	std::expected<void, Error> EditorContext::RequireEditing() const
	{
		if (m_PlayState != PlayState::Editing)
			return std::unexpected(
				Error(ErrorCode::InvalidState, "Stop play mode first: changes made while playing are thrown away"));
		return {};
	}

	std::vector<InputCommand> EditorContext::SampleInput(uint64_t tick, const Simulation::InputSampler& live)
	{
		if (!m_QueuedInput.empty())
		{
			InputCommand command = m_QueuedInput.front();
			m_QueuedInput.pop_front();
			command.Tick = tick;
			command.Player = 0;
			return {command};
		}
		if (live)
			return live(tick);
		return {};
	}

	void EditorContext::RescanAssets()
	{
		if (m_Assets)
		{
			if (auto scanned = ScanAssets(); !scanned)
				LS_CORE_ERROR("Scanning the project's assets failed: {}", scanned.error());
		}
	}

}
