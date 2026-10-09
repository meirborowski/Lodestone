#pragma once

#include "Lodestone/Core/Error.h"
#include "Lodestone/Serialization/Json.h"

#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <vector>

namespace Lodestone {

	// Upgrades a document from one version of its format to the next, in place
	using Migration = std::function<std::expected<void, Error>(Json::Value& document)>;

	// A versioned file format, such as scenes or asset metadata (see docs/Architecture.md#file-formats). Documents
	// start with their format's name and version: {"format": "Lodestone.Scene", "version": 1, ...}. Changing a format
	// means bumping its version and adding a migration from the previous one, tested against a fixture file in the old
	// format
	struct FileFormat
	{
		std::string Name;
		uint32_t CurrentVersion = 1;
		// Migrations[i] upgrades version i + 1 to version i + 2, so there's one fewer than the current version
		std::vector<Migration> Migrations;
	};

	// An empty document of the format's current version
	Json::Value CreateDocument(const FileFormat& format);

	// Checks that a document is of the format, and upgrades it to the current version one migration at a time. A
	// document from a newer version of the format fails with UnsupportedVersion, so it's never loaded partially
	[[nodiscard]] std::expected<void, Error> UpgradeDocument(Json::Value& document, const FileFormat& format);

}
