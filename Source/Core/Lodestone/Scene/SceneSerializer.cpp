#include "Lodestone/Scene/SceneSerializer.h"

#include "Lodestone/Core/FileSystem.h"
#include "Lodestone/Scene/EntitySerializer.h"
#include "Lodestone/Serialization/Json.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/std.h>

#include <array>
#include <string>

namespace Lodestone {

	namespace {

		constexpr const char* EntitiesKey = "entities";

		std::expected<Scope<Scene>, Error> DeserializeScene(
			const Json::Value& document, const ComponentRegistry& components)
		{
			constexpr std::array<std::string_view, 3> documentMembers = {"format", "version", EntitiesKey};
			if (auto checked = Json::CheckMembers(document, documentMembers, "The scene"); !checked)
				return std::unexpected(checked.error());
			const auto entities = Json::GetMember(document, EntitiesKey, "The scene");
			if (!entities)
				return std::unexpected(entities.error());

			auto scene = CreateScope<Scene>(components);
			if (auto created = EntitySerializer::DeserializeEntities(*scene, **entities, EntitiesKey); !created)
				return std::unexpected(created.error());
			return scene;
		}

	}

	const FileFormat& SceneSerializer::GetFormat()
	{
		static const FileFormat Format{.Name = "Lodestone.Scene", .CurrentVersion = 1, .Migrations = {}};
		return Format;
	}

	Json::Value SceneSerializer::Serialize(const Scene& scene)
	{
		Json::Value document = CreateDocument(GetFormat());
		document[EntitiesKey] = EntitySerializer::SerializeScene(scene);
		return document;
	}

	std::expected<Scope<Scene>, Error> SceneSerializer::Deserialize(
		Json::Value document, const ComponentRegistry& components)
	{
		if (auto upgraded = UpgradeDocument(document, GetFormat()); !upgraded)
			return std::unexpected(upgraded.error());
		// The checks above leave nothing for nlohmann/json to throw, but the document comes from outside
		try
		{
			return DeserializeScene(document, components);
		}
		catch (const Json::Value::exception& exception)
		{
			return std::unexpected(Error(ErrorCode::ParseError, exception.what()));
		}
	}

	std::string SceneSerializer::SerializeToText(const Scene& scene)
	{
		return Json::Write(Serialize(scene));
	}

	std::expected<Scope<Scene>, Error> SceneSerializer::DeserializeFromText(
		std::string_view text, const ComponentRegistry& components)
	{
		auto document = Json::Parse(text);
		if (!document)
			return std::unexpected(document.error());
		return Deserialize(std::move(*document), components);
	}

	std::expected<void, Error> SceneSerializer::Save(const Scene& scene, const std::filesystem::path& path)
	{
		return WriteFileAtomically(path, SerializeToText(scene))
			.transform_error(
				[&path](const Error& error) { return error.WithContext(fmt::format("Saving scene {}", path)); });
	}

	std::expected<Scope<Scene>, Error> SceneSerializer::Load(
		const std::filesystem::path& path, const ComponentRegistry& components)
	{
		const auto text = ReadFile(path);
		if (!text)
			return std::unexpected(text.error().WithContext(fmt::format("Loading scene {}", path)));
		return DeserializeFromText(*text, components)
			.transform_error(
				[&path](const Error& error) { return error.WithContext(fmt::format("Loading scene {}", path)); });
	}

}
