#include "Lodestone/Serialization/FileFormat.h"

#include "Lodestone/Core/Assert.h"
#include "Lodestone/Serialization/Json.h"

#include <spdlog/fmt/fmt.h>

namespace Lodestone {

	namespace {

		constexpr const char* FormatKey = "format";
		constexpr const char* VersionKey = "version";

	}

	Json::Value CreateDocument(const FileFormat& format)
	{
		Json::Value document = Json::Value::object();
		document[FormatKey] = format.Name;
		document[VersionKey] = format.CurrentVersion;
		return document;
	}

	std::expected<void, Error> UpgradeDocument(Json::Value& document, const FileFormat& format)
	{
		LS_CORE_ASSERT(format.CurrentVersion >= 1 && format.Migrations.size() == format.CurrentVersion - 1,
			"File format {} needs a migration for every version before {}", format.Name, format.CurrentVersion);

		const auto name = Json::GetString(document, FormatKey, "The document");
		if (!name)
			return std::unexpected(name.error());
		if (*name != format.Name)
			return std::unexpected(Error(
				ErrorCode::ParseError, fmt::format("The document is a {} file, not a {} file", *name, format.Name)));

		const auto version = Json::GetUInt(document, VersionKey, "The document");
		if (!version)
			return std::unexpected(version.error());
		if (*version == 0)
			return std::unexpected(Error(ErrorCode::ParseError, "The document has version 0, which never existed"));
		if (*version > format.CurrentVersion)
			return std::unexpected(Error(ErrorCode::UnsupportedVersion,
				fmt::format(
					"The {} file is version {}, from a newer version of Lodestone; this one reads up to version "
					"{}",
					format.Name, *version, format.CurrentVersion)));

		for (uint32_t from = *version; from < format.CurrentVersion; ++from)
		{
			const Migration& migration = format.Migrations[from - 1];
			std::expected<void, Error> migrated;
			// Migrations may use nlohmann/json's checked accessors, whose exceptions must not escape
			try
			{
				migrated = migration(document);
			}
			catch (const Json::Value::exception& exception)
			{
				migrated = std::unexpected(Error(ErrorCode::ParseError, exception.what()));
			}
			if (!migrated)
				return std::unexpected(migrated.error().WithContext(
					fmt::format("Upgrading the {} file from version {} to {}", format.Name, from, from + 1)));
			document[VersionKey] = from + 1;
		}
		return {};
	}

}
