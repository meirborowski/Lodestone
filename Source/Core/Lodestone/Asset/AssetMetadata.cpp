#include "Lodestone/Asset/AssetMetadata.h"

#include "Lodestone/Core/FileSystem.h"
#include "Lodestone/Serialization/Json.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/std.h>

#include <array>
#include <utility>

namespace Lodestone {

	namespace {

		constexpr std::array AssetTypeNames = {
			std::pair{AssetType::Scene, std::string_view("Scene")},
			std::pair{AssetType::Prefab, std::string_view("Prefab")},
			std::pair{AssetType::Texture, std::string_view("Texture")},
			std::pair{AssetType::Model, std::string_view("Model")},
			std::pair{AssetType::Environment, std::string_view("Environment")},
			std::pair{AssetType::Audio, std::string_view("Audio")},
			std::pair{AssetType::Font, std::string_view("Font")},
			std::pair{AssetType::Script, std::string_view("Script")},
		};

		constexpr std::array AssetExtensions = {
			std::pair{std::string_view(".lscene"), AssetType::Scene},
			std::pair{std::string_view(".lprefab"), AssetType::Prefab},
			std::pair{std::string_view(".png"), AssetType::Texture},
			std::pair{std::string_view(".jpg"), AssetType::Texture},
			std::pair{std::string_view(".jpeg"), AssetType::Texture},
			std::pair{std::string_view(".gltf"), AssetType::Model},
			std::pair{std::string_view(".glb"), AssetType::Model},
			std::pair{std::string_view(".hdr"), AssetType::Environment},
			std::pair{std::string_view(".wav"), AssetType::Audio},
			std::pair{std::string_view(".flac"), AssetType::Audio},
			std::pair{std::string_view(".mp3"), AssetType::Audio},
			std::pair{std::string_view(".ttf"), AssetType::Font},
			std::pair{std::string_view(".lua"), AssetType::Script},
		};

		constexpr const char* IdKey = "id";
		constexpr const char* TypeKey = "type";
		constexpr const char* ImportSettingsKey = "importSettings";

		std::expected<AssetMetadata, Error> Deserialize(Json::Value document)
		{
			if (auto upgraded = UpgradeDocument(document, AssetMetadataSerializer::GetFormat()); !upgraded)
				return std::unexpected(upgraded.error());
			constexpr std::array<std::string_view, 5> members = {
				"format", "version", IdKey, TypeKey, ImportSettingsKey};
			if (auto checked = Json::CheckMembers(document, members, "The asset metadata"); !checked)
				return std::unexpected(checked.error());

			AssetMetadata metadata;
			const auto id = Json::GetUUID(document, IdKey, "The asset metadata");
			if (!id)
				return std::unexpected(id.error());
			if (id->IsNil())
				return std::unexpected(Error(ErrorCode::ParseError, "The asset metadata has the nil UUID"));
			metadata.Id = *id;

			const auto typeName = Json::GetString(document, TypeKey, "The asset metadata");
			if (!typeName)
				return std::unexpected(typeName.error());
			const std::optional<AssetType> type = AssetTypeFromString(*typeName);
			if (!type)
				return std::unexpected(
					Error(ErrorCode::ParseError, fmt::format("The asset metadata has unknown type '{}'", *typeName)));
			metadata.Type = *type;

			const auto settings = Json::GetMember(document, ImportSettingsKey, "The asset metadata");
			if (!settings)
				return std::unexpected(settings.error());
			if (!(*settings)->is_object())
				return std::unexpected(Error(ErrorCode::ParseError,
					fmt::format(
						"The asset metadata's import settings: expected an object, not {}", (*settings)->type_name())));
			metadata.ImportSettings = **settings;
			return metadata;
		}

	}

	std::string_view ToString(AssetType type)
	{
		for (const auto& [value, name] : AssetTypeNames)
		{
			if (value == type)
				return name;
		}
		return "Unknown";
	}

	std::optional<AssetType> AssetTypeFromString(std::string_view name)
	{
		for (const auto& [value, valueName] : AssetTypeNames)
		{
			if (valueName == name)
				return value;
		}
		return std::nullopt;
	}

	std::optional<AssetType> GetAssetTypeForFile(const std::filesystem::path& path)
	{
		// Extensions are compared in ASCII lowercase, without the system's code page or locale
		std::string extension;
		for (const char8_t character : path.extension().u8string())
			extension.push_back(
				static_cast<char>(character >= u8'A' && character <= u8'Z' ? character - u8'A' + u8'a' : character));
		for (const auto& [assetExtension, type] : AssetExtensions)
		{
			if (assetExtension == extension)
				return type;
		}
		return std::nullopt;
	}

	const FileFormat& AssetMetadataSerializer::GetFormat()
	{
		static const FileFormat Format{.Name = "Lodestone.AssetMetadata", .CurrentVersion = 1, .Migrations = {}};
		return Format;
	}

	std::filesystem::path AssetMetadataSerializer::GetMetadataPath(const std::filesystem::path& assetPath)
	{
		std::filesystem::path metadataPath = assetPath;
		metadataPath += ".meta";
		return metadataPath;
	}

	std::string AssetMetadataSerializer::SerializeToText(const AssetMetadata& metadata)
	{
		Json::Value document = CreateDocument(GetFormat());
		document[IdKey] = metadata.Id.ToString();
		document[TypeKey] = ToString(metadata.Type);
		document[ImportSettingsKey] = metadata.ImportSettings;
		return Json::Write(document);
	}

	std::expected<AssetMetadata, Error> AssetMetadataSerializer::DeserializeFromText(std::string_view text)
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

	std::expected<void, Error> AssetMetadataSerializer::Save(
		const AssetMetadata& metadata, const std::filesystem::path& path)
	{
		return WriteFileAtomically(path, SerializeToText(metadata))
			.transform_error([&path](const Error& error)
				{ return error.WithContext(fmt::format("Saving asset metadata {}", path)); });
	}

	std::expected<AssetMetadata, Error> AssetMetadataSerializer::Load(const std::filesystem::path& path)
	{
		const auto text = ReadFile(path);
		if (!text)
			return std::unexpected(text.error().WithContext(fmt::format("Loading asset metadata {}", path)));
		return DeserializeFromText(*text).transform_error(
			[&path](const Error& error) { return error.WithContext(fmt::format("Loading asset metadata {}", path)); });
	}

}
