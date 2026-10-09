#include "Lodestone/Project/Project.h"

#include "Lodestone/Core/FileSystem.h"
#include "Lodestone/Serialization/Json.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/std.h>

#include <array>
#include <system_error>
#include <utility>

namespace Lodestone {

	namespace {

		constexpr const char* NameKey = "name";
		constexpr const char* StartupSceneKey = "startupScene";
		constexpr const char* TickRateKey = "tickRate";

		std::expected<ProjectSettings, Error> Deserialize(Json::Value document)
		{
			if (auto upgraded = UpgradeDocument(document, Project::GetFormat()); !upgraded)
				return std::unexpected(upgraded.error());
			constexpr std::array<std::string_view, 5> members = {
				"format", "version", NameKey, StartupSceneKey, TickRateKey};
			if (auto checked = Json::CheckMembers(document, members, "The project"); !checked)
				return std::unexpected(checked.error());

			ProjectSettings settings;
			auto name = Json::GetString(document, NameKey, "The project");
			if (!name)
				return std::unexpected(name.error());
			settings.Name = std::move(*name);
			const auto startupScene = Json::GetUUID(document, StartupSceneKey, "The project");
			if (!startupScene)
				return std::unexpected(startupScene.error());
			settings.StartupScene = *startupScene;
			const auto tickRate = Json::GetUInt(document, TickRateKey, "The project");
			if (!tickRate)
				return std::unexpected(tickRate.error());
			if (*tickRate == 0 || *tickRate > Project::MaxTickRate)
				return std::unexpected(Error(ErrorCode::ParseError,
					fmt::format(
						"The project's tick rate must be between 1 and {}, not {}", Project::MaxTickRate, *tickRate)));
			settings.TickRate = *tickRate;
			return settings;
		}

	}

	const FileFormat& Project::GetFormat()
	{
		static const FileFormat Format{.Name = "Lodestone.Project", .CurrentVersion = 1, .Migrations = {}};
		return Format;
	}

	Project::Project(std::filesystem::path directory, ProjectSettings settings)
		: m_Directory(std::move(directory)), m_Settings(std::move(settings))
	{
	}

	std::expected<Project, Error> Project::Create(const std::filesystem::path& directory, std::string_view name)
	{
		if (name.empty())
			return std::unexpected(Error(ErrorCode::InvalidArgument, "A project needs a name"));
		std::error_code error;
		if (std::filesystem::exists(directory / FileName, error))
			return std::unexpected(Error(ErrorCode::AlreadyExists, fmt::format("{} already has a project", directory)));
		std::filesystem::create_directories(directory / AssetDirectoryName, error);
		if (error)
			return std::unexpected(
				Error(ErrorCode::IoError, fmt::format("Can't create the project directory {}: {}", directory, error)));

		Project project(directory, ProjectSettings{.Name = std::string(name)});
		if (auto saved = project.Save(); !saved)
			return std::unexpected(saved.error());
		return project;
	}

	std::expected<Project, Error> Project::Open(const std::filesystem::path& directory)
	{
		const auto text = ReadFile(directory / FileName);
		if (!text)
			return std::unexpected(text.error().WithContext(fmt::format("Opening the project in {}", directory)));
		auto settings = DeserializeFromText(*text);
		if (!settings)
			return std::unexpected(settings.error().WithContext(fmt::format("Opening the project in {}", directory)));

		std::error_code error;
		std::filesystem::create_directories(directory / AssetDirectoryName, error);
		if (error)
			return std::unexpected(Error(ErrorCode::IoError,
				fmt::format("Can't create the asset directory {}: {}", directory / AssetDirectoryName, error)));
		return Project(directory, std::move(*settings));
	}

	std::expected<void, Error> Project::Save() const
	{
		return WriteFileAtomically(GetFilePath(), SerializeToText(m_Settings))
			.transform_error([this](const Error& error)
				{ return error.WithContext(fmt::format("Saving the project {}", GetFilePath())); });
	}

	std::string Project::SerializeToText(const ProjectSettings& settings)
	{
		Json::Value document = CreateDocument(GetFormat());
		document[NameKey] = settings.Name;
		document[StartupSceneKey] = settings.StartupScene.ToString();
		document[TickRateKey] = settings.TickRate;
		return Json::Write(document);
	}

	std::expected<ProjectSettings, Error> Project::DeserializeFromText(std::string_view text)
	{
		auto document = Json::Parse(text);
		if (!document)
			return std::unexpected(document.error());
		try
		{
			return Deserialize(std::move(*document));
		}
		catch (const Json::Value::exception& exception)
		{
			return std::unexpected(Error(ErrorCode::ParseError, exception.what()));
		}
	}

}
