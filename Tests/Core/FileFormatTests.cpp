#include "Lodestone/Serialization/FileFormat.h"

#include "Common/DescribeError.h"
#include "Lodestone/Core/FileSystem.h"
#include "Lodestone/Serialization/Json.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

namespace Lodestone {

	namespace {

		// A format at version 3. Version 2 renamed "size" to "scale"; version 3 added "tags"
		FileFormat MakeTestFormat()
		{
			FileFormat format;
			format.Name = "Lodestone.Test";
			format.CurrentVersion = 3;
			format.Migrations.emplace_back(
				[](Json::Value& document) -> std::expected<void, Error>
				{
					if (!document.contains("size"))
						return std::unexpected(Error(ErrorCode::ParseError, "'size' is missing"));
					document["scale"] = document["size"];
					document.erase("size");
					return {};
				});
			format.Migrations.emplace_back(
				[](Json::Value& document) -> std::expected<void, Error>
				{
					document["tags"] = Json::Value::array();
					return {};
				});
			return format;
		}

		Json::Value LoadFixture(std::string_view name)
		{
			const auto text = ReadFile(std::filesystem::path(LS_TEST_FIXTURE_DIR) / "FileFormat" / name);
			REQUIRE_MESSAGE(text.has_value(), Testing::DescribeError(text));
			auto document = Json::Parse(*text);
			REQUIRE(document.has_value());
			return *document;
		}

		// What the fixtures become at the current version
		Json::Value MakeCurrentDocument()
		{
			return {{"format", "Lodestone.Test"}, {"version", 3}, {"name", "Widget"}, {"scale", 2},
				{"tags", Json::Value::array()}};
		}

	}

	TEST_CASE("CreateDocument starts a document of the format's current version")
	{
		const Json::Value document = CreateDocument(MakeTestFormat());
		CHECK(document == Json::Value({{"format", "Lodestone.Test"}, {"version", 3}}));
	}

	TEST_CASE("Version 1 documents are upgraded through every migration")
	{
		Json::Value document = LoadFixture("TestFormat.v1.json");
		REQUIRE(UpgradeDocument(document, MakeTestFormat()).has_value());
		CHECK(document == MakeCurrentDocument());
	}

	TEST_CASE("Version 2 documents are upgraded through the last migration")
	{
		Json::Value document = LoadFixture("TestFormat.v2.json");
		REQUIRE(UpgradeDocument(document, MakeTestFormat()).has_value());
		CHECK(document == MakeCurrentDocument());
	}

	TEST_CASE("Current documents are left as they are")
	{
		Json::Value document = MakeCurrentDocument();
		REQUIRE(UpgradeDocument(document, MakeTestFormat()).has_value());
		CHECK(document == MakeCurrentDocument());
	}

	TEST_CASE("Documents from a newer version fail to load")
	{
		Json::Value document = MakeCurrentDocument();
		document["version"] = 4;
		const auto upgraded = UpgradeDocument(document, MakeTestFormat());
		REQUIRE_FALSE(upgraded.has_value());
		CHECK(upgraded.error().GetCode() == ErrorCode::UnsupportedVersion);
		CHECK(upgraded.error().GetMessageText().contains("version 4"));
	}

	TEST_CASE("Documents of another format, or without a valid version, are rejected")
	{
		const FileFormat format = MakeTestFormat();
		const auto expectRejected = [&format](Json::Value document)
		{
			CAPTURE(document.dump());
			const auto upgraded = UpgradeDocument(document, format);
			REQUIRE_FALSE(upgraded.has_value());
			CHECK(upgraded.error().GetCode() == ErrorCode::ParseError);
		};
		expectRejected({{"format", "Lodestone.Other"}, {"version", 1}});
		expectRejected({{"version", 1}});
		expectRejected({{"format", "Lodestone.Test"}});
		expectRejected({{"format", "Lodestone.Test"}, {"version", 0}});
		expectRejected({{"format", "Lodestone.Test"}, {"version", "1"}});
		expectRejected({{"format", "Lodestone.Test"}, {"version", -1}});
		expectRejected(Json::Value::array());
	}

	TEST_CASE("A failing migration fails the upgrade, saying which one")
	{
		Json::Value document = {{"format", "Lodestone.Test"}, {"version", 1}, {"name", "No size"}};
		const auto upgraded = UpgradeDocument(document, MakeTestFormat());
		REQUIRE_FALSE(upgraded.has_value());
		CHECK(upgraded.error().GetMessageText().contains("from version 1 to 2"));
		CHECK(upgraded.error().GetMessageText().contains("'size' is missing"));
	}

	TEST_CASE("A migration that throws a JSON exception fails the upgrade instead of crashing")
	{
		FileFormat format;
		format.Name = "Lodestone.Test";
		format.CurrentVersion = 2;
		format.Migrations.emplace_back(
			[](Json::Value& document) -> std::expected<void, Error>
			{
				document["missing"] = document.at("absent");
				return {};
			});
		Json::Value document = {{"format", "Lodestone.Test"}, {"version", 1}};

		const auto upgraded = UpgradeDocument(document, format);
		REQUIRE_FALSE(upgraded.has_value());
		CHECK(upgraded.error().GetCode() == ErrorCode::ParseError);
	}

}
