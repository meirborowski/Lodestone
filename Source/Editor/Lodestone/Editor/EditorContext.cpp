#include "Lodestone/Editor/EditorContext.h"

#include "Lodestone/Core/FileSystem.h"
#include "Lodestone/Editor/Commands/EntityCommands.h"
#include "Lodestone/Scene/Entity.h"
#include "Lodestone/Scene/EntitySerializer.h"
#include "Lodestone/Scene/PrefabSerializer.h"
#include "Lodestone/Scene/SceneSerializer.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/std.h>

#include <system_error>
#include <utility>

namespace Lodestone {

	namespace {

		constexpr uint32_t DefaultTickRate = 60;

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
		: m_Components(&components), m_Scene(CreateScope<Scene>(components)), m_SavedStateId(m_History.GetStateId())
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
		m_Project = CreateScope<Project>(std::move(*project));
		m_Assets = CreateScope<AssetRegistry>(m_Project->GetAssetDirectory());
		RescanAssets();
		return NewScene();
	}

	std::expected<void, Error> EditorContext::OpenProject(const std::filesystem::path& directory)
	{
		if (auto editing = RequireEditing(); !editing)
			return editing;
		auto project = Project::Open(directory);
		if (!project)
			return std::unexpected(project.error());
		m_Project = CreateScope<Project>(std::move(*project));
		m_Assets = CreateScope<AssetRegistry>(m_Project->GetAssetDirectory());
		RescanAssets();
		return NewScene();
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
		const uint32_t tickRate = m_Project ? m_Project->GetSettings().TickRate : DefaultTickRate;
		m_Simulation = CreateScope<Simulation>(*m_Scene, SimulationConfig{.TickRate = tickRate});
		for (const auto& [name, system] : m_PlaySystems)
			m_Simulation->AddSystem(name, system);
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
		m_Simulation.reset();
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
		if (m_PlayState != PlayState::Playing)
			return;
		m_Simulation->Advance(elapsedSeconds, [this, &live](uint64_t tick) { return SampleInput(tick, live); });
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
			if (auto scanned = m_Assets->Scan(); !scanned)
				LS_CORE_ERROR("Scanning the project's assets failed: {}", scanned.error());
		}
	}

}
