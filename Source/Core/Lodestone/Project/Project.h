#pragma once

#include "Lodestone/Core/Error.h"
#include "Lodestone/Core/UUID.h"
#include "Lodestone/Serialization/FileFormat.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace Lodestone {

	struct ProjectSettings
	{
		std::string Name;
		// The scene a game starts with: an asset UUID, or nil when there's none yet
		UUID StartupScene;
		// Simulation ticks per second (see docs/Architecture.md#simulation)
		uint32_t TickRate = 60;

		bool operator==(const ProjectSettings& other) const = default;
	};

	// A game project: a directory with a project file (Project.lsproject) and its assets (Assets/):
	//
	//   {"format": "Lodestone.Project", "version": 1, "name": "Pong", "startupScene": "...", "tickRate": 60}
	class Project
	{
	public:
		static constexpr std::string_view FileName = "Project.lsproject";
		static constexpr std::string_view AssetDirectoryName = "Assets";
		static constexpr uint32_t MaxTickRate = 1000;

		static const FileFormat& GetFormat();

		// Creates a project, with a name, in a directory that has no project yet (it's created if it doesn't exist)
		[[nodiscard]] static std::expected<Project, Error> Create(
			const std::filesystem::path& directory, std::string_view name);
		// Opens the project in a directory
		[[nodiscard]] static std::expected<Project, Error> Open(const std::filesystem::path& directory);

		[[nodiscard]] std::expected<void, Error> Save() const;

		const std::filesystem::path& GetDirectory() const { return m_Directory; }
		std::filesystem::path GetAssetDirectory() const { return m_Directory / AssetDirectoryName; }
		std::filesystem::path GetFilePath() const { return m_Directory / FileName; }

		const ProjectSettings& GetSettings() const { return m_Settings; }
		ProjectSettings& GetSettings() { return m_Settings; }

		static std::string SerializeToText(const ProjectSettings& settings);
		[[nodiscard]] static std::expected<ProjectSettings, Error> DeserializeFromText(std::string_view text);

	private:
		Project(std::filesystem::path directory, ProjectSettings settings);

	private:
		std::filesystem::path m_Directory;
		ProjectSettings m_Settings;
	};

}
