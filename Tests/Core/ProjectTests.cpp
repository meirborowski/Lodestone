#include "Lodestone/Project/Project.h"

#include "Common/DescribeError.h"
#include "Common/TemporaryDirectory.h"
#include "Lodestone/Core/FileSystem.h"
#include "Lodestone/Serialization/Json.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

namespace Lodestone {

	TEST_CASE("Creating a project writes its file and asset directory")
	{
		const Testing::TemporaryDirectory directory("ProjectCreate");
		const std::filesystem::path root = directory.GetPath() / "Pong";

		const auto project = Project::Create(root, "Pong");

		REQUIRE_MESSAGE(project.has_value(), Testing::DescribeError(project));
		CHECK(project->GetSettings().Name == "Pong");
		CHECK(project->GetSettings().TickRate == 60);
		CHECK(project->GetSettings().StartupScene.IsNil());
		CHECK(std::filesystem::is_regular_file(root / "Project.lsproject"));
		CHECK(std::filesystem::is_directory(root / "Assets"));
		CHECK(project->GetAssetDirectory() == root / "Assets");
	}

	TEST_CASE("A project needs a name")
	{
		const Testing::TemporaryDirectory directory("ProjectUnnamed");

		const auto project = Project::Create(directory.GetPath(), "");

		REQUIRE_FALSE(project.has_value());
		CHECK(project.error().GetCode() == ErrorCode::InvalidArgument);
		CHECK_FALSE(std::filesystem::exists(directory.GetPath() / "Project.lsproject"));
	}

	TEST_CASE("A project can't be created where one exists")
	{
		const Testing::TemporaryDirectory directory("ProjectExists");
		REQUIRE(Project::Create(directory.GetPath(), "First").has_value());

		const auto second = Project::Create(directory.GetPath(), "Second");

		REQUIRE_FALSE(second.has_value());
		CHECK(second.error().GetCode() == ErrorCode::AlreadyExists);
	}

	TEST_CASE("A saved project opens with its settings")
	{
		const Testing::TemporaryDirectory directory("ProjectOpen");
		auto created = Project::Create(directory.GetPath(), "Tetris");
		REQUIRE(created.has_value());
		created->GetSettings().TickRate = 120;
		created->GetSettings().StartupScene = UUID::FromRandomBits(0x1111'2222'3333'4444ull, 0x8555'6666'7777'8888ull);
		REQUIRE(created->Save().has_value());

		const auto opened = Project::Open(directory.GetPath());

		REQUIRE_MESSAGE(opened.has_value(), Testing::DescribeError(opened));
		CHECK(opened->GetSettings() == created->GetSettings());
		CHECK(opened->GetDirectory() == directory.GetPath());
	}

	TEST_CASE("Version 1 project files load")
	{
		const auto text = ReadFile(std::filesystem::path(LS_TEST_FIXTURE_DIR) / "Projects" / "Pong.v1.lsproject");
		REQUIRE(text.has_value());

		const auto settings = Project::DeserializeFromText(*text);

		REQUIRE_MESSAGE(settings.has_value(), Testing::DescribeError(settings));
		CHECK(settings->Name == "Pong");
		CHECK(settings->StartupScene == UUID(0x66666666'6666'4666ull, 0x8666'666666666666ull));
		CHECK(settings->TickRate == 120);
	}

	TEST_CASE("Opening a directory without a project fails")
	{
		const Testing::TemporaryDirectory directory("ProjectMissing");

		const auto opened = Project::Open(directory.GetPath());

		REQUIRE_FALSE(opened.has_value());
		CHECK(opened.error().GetCode() == ErrorCode::FileNotFound);
	}

	TEST_CASE("Project settings round-trip through text")
	{
		const ProjectSettings settings{.Name = "Snake \"Deluxe\"",
			.StartupScene = UUID::FromRandomBits(0xAAAA'BBBB'CCCC'4DDDull, 0x8EEE'FFFF'0000'1111ull),
			.TickRate = 30};

		const auto loaded = Project::DeserializeFromText(Project::SerializeToText(settings));

		REQUIRE(loaded.has_value());
		CHECK(*loaded == settings);
	}

	TEST_CASE("Invalid project files fail to load")
	{
		const std::string valid = Project::SerializeToText({.Name = "Game"});
		auto document = Json::Parse(valid);
		REQUIRE(document.has_value());

		SUBCASE("A tick rate of zero")
		{
			(*document)["tickRate"] = 0;
		}
		SUBCASE("A tick rate above the maximum")
		{
			(*document)["tickRate"] = Project::MaxTickRate + 1;
		}
		SUBCASE("A missing name")
		{
			document->erase("name");
		}
		SUBCASE("An unknown member")
		{
			(*document)["editor"] = true;
		}
		SUBCASE("A startup scene that isn't a UUID")
		{
			(*document)["startupScene"] = "Main.lscene";
		}
		SUBCASE("A newer version")
		{
			(*document)["version"] = 2;
		}

		CHECK_FALSE(Project::DeserializeFromText(Json::Write(*document)).has_value());
	}

}
