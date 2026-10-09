#include "Common/DescribeError.h"
#include "Common/TemporaryDirectory.h"
#include "Lodestone/Asset/AssetMetadata.h"
#include "Lodestone/Asset/AssetRegistry.h"
#include "Lodestone/Core/FileSystem.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace Lodestone {

	namespace {

		void WriteText(const std::filesystem::path& path, std::string_view text)
		{
			std::filesystem::create_directories(path.parent_path());
			REQUIRE(WriteFileAtomically(path, text).has_value());
		}

		bool Contains(const std::vector<std::string>& paths, std::string_view path)
		{
			return std::ranges::find(paths, path) != paths.end();
		}

	}

	TEST_CASE("Asset types come from file extensions, in any case")
	{
		CHECK(GetAssetTypeForFile("Textures/Brick.png") == AssetType::Texture);
		CHECK(GetAssetTypeForFile("Textures/Brick.JPEG") == AssetType::Texture);
		CHECK(GetAssetTypeForFile("Level.lscene") == AssetType::Scene);
		CHECK(GetAssetTypeForFile("Enemy.lprefab") == AssetType::Prefab);
		CHECK(GetAssetTypeForFile("Robot.glb") == AssetType::Model);
		CHECK(GetAssetTypeForFile("Sky.hdr") == AssetType::Environment);
		CHECK(GetAssetTypeForFile("Music.flac") == AssetType::Audio);
		CHECK(GetAssetTypeForFile("Font.ttf") == AssetType::Font);
		CHECK(GetAssetTypeForFile("Player.lua") == AssetType::Script);
		CHECK_FALSE(GetAssetTypeForFile("Notes.txt").has_value());
		CHECK_FALSE(GetAssetTypeForFile("Brick.png.meta").has_value());
		CHECK_FALSE(GetAssetTypeForFile("README").has_value());
	}

	TEST_CASE("Asset type names round-trip")
	{
		for (const AssetType type : {AssetType::Scene, AssetType::Prefab, AssetType::Texture, AssetType::Model,
				 AssetType::Environment, AssetType::Audio, AssetType::Font, AssetType::Script})
			CHECK(AssetTypeFromString(ToString(type)) == type);
		CHECK_FALSE(AssetTypeFromString("Mesh").has_value());
	}

	TEST_CASE("Asset metadata round-trips through its file format")
	{
		AssetMetadata metadata;
		metadata.Id = UUID::Generate();
		metadata.Type = AssetType::Texture;
		metadata.ImportSettings = {{"srgb", true}, {"maxSize", 2048}};

		const std::string text = AssetMetadataSerializer::SerializeToText(metadata);
		const auto loaded = AssetMetadataSerializer::DeserializeFromText(text);
		REQUIRE_MESSAGE(loaded.has_value(), Testing::DescribeError(loaded));
		CHECK(*loaded == metadata);
		CHECK(text.starts_with("{\n  \"format\": \"Lodestone.AssetMetadata\""));
	}

	TEST_CASE("Version 1 asset metadata files load")
	{
		const auto loaded =
			AssetMetadataSerializer::Load(std::filesystem::path(LS_TEST_FIXTURE_DIR) / "Assets" / "Brick.png.v1.meta");
		REQUIRE_MESSAGE(loaded.has_value(), Testing::DescribeError(loaded));
		// 8f14e45f-ceea-467a-9575-d8e1a3d1c2b0
		CHECK(loaded->Id == UUID(0x8f14e45f'ceea'467aull, 0x9575'd8e1a3d1c2b0ull));
		CHECK(loaded->Type == AssetType::Texture);
		CHECK(loaded->ImportSettings == Json::Value({{"srgb", true}}));
	}

	TEST_CASE("Invalid asset metadata fails to load")
	{
		const auto expectInvalid = [](std::string_view text)
		{
			CAPTURE(text);
			CHECK_FALSE(AssetMetadataSerializer::DeserializeFromText(text).has_value());
		};
		const std::string valid =
			R"({"format": "Lodestone.AssetMetadata", "version": 1, )"
			R"("id": "8f14e45f-ceea-467a-9575-d8e1a3d1c2b0", "type": "Texture", "importSettings": {}})";
		REQUIRE(AssetMetadataSerializer::DeserializeFromText(valid).has_value());

		expectInvalid(
			R"({"format": "Lodestone.AssetMetadata", "version": 1, "type": "Texture", "importSettings": {}})");
		expectInvalid(
			R"({"format": "Lodestone.AssetMetadata", "version": 1, "id": "00000000-0000-0000-0000-000000000000", "type": "Texture", "importSettings": {}})");
		expectInvalid(
			R"({"format": "Lodestone.AssetMetadata", "version": 1, "id": "8f14e45f-ceea-467a-9575-d8e1a3d1c2b0", "type": "Mesh", "importSettings": {}})");
		expectInvalid(
			R"({"format": "Lodestone.AssetMetadata", "version": 1, "id": "8f14e45f-ceea-467a-9575-d8e1a3d1c2b0", "type": "Texture", "importSettings": []})");
		expectInvalid(
			R"({"format": "Lodestone.AssetMetadata", "version": 1, "id": "8f14e45f-ceea-467a-9575-d8e1a3d1c2b0", "type": "Texture", "importSettings": {}, "extra": 1})");
		expectInvalid(R"({"format": "Lodestone.Scene", "version": 1, "entities": []})");
		expectInvalid("not json");
	}

	TEST_CASE("Scanning creates metadata for new assets and ignores other files")
	{
		const Testing::TemporaryDirectory directory("AssetScan");
		const std::filesystem::path& root = directory.GetPath();
		WriteText(root / "Textures" / "Brick.png", "png");
		WriteText(root / "Level.lscene", "{}");
		WriteText(root / "Notes.txt", "not an asset");
		WriteText(root / ".hidden" / "Secret.png", "hidden");
		WriteText(root / ".Thumbs.png", "hidden");

		AssetRegistry registry(root);
		const auto report = registry.Scan();
		REQUIRE_MESSAGE(report.has_value(), Testing::DescribeError(report));

		CHECK(registry.GetAssetCount() == 2);
		CHECK(report->CreatedMetadata.size() == 2);
		CHECK(Contains(report->CreatedMetadata, "Textures/Brick.png"));
		CHECK(std::filesystem::exists(root / "Textures" / "Brick.png.meta"));
		CHECK_FALSE(std::filesystem::exists(root / "Notes.txt.meta"));
		CHECK_FALSE(std::filesystem::exists(root / ".hidden" / "Secret.png.meta"));

		const AssetInfo* brick = registry.FindByPath("Textures/Brick.png");
		REQUIRE(brick != nullptr);
		CHECK(brick->Metadata.Type == AssetType::Texture);
		CHECK(registry.Find(brick->Metadata.Id) == brick);
		CHECK(registry.GetAbsolutePath(*brick) == root / "Textures" / "Brick.png");
		CHECK(registry.FindByPath("Notes.txt") == nullptr);
	}

	TEST_CASE("Every asset is listed, sorted by path")
	{
		const Testing::TemporaryDirectory directory("AssetList");
		const std::filesystem::path& root = directory.GetPath();
		WriteText(root / "Scenes" / "Main.lscene", "{}");
		WriteText(root / "Audio.wav", "wav");
		WriteText(root / "Textures" / "Brick.png", "png");
		AssetRegistry registry(root);
		REQUIRE(registry.Scan().has_value());

		std::vector<std::string> paths;
		for (const AssetInfo* asset : registry.GetAll())
			paths.push_back(asset->Path);

		CHECK(paths == std::vector<std::string>{"Audio.wav", "Scenes/Main.lscene", "Textures/Brick.png"});
	}

	TEST_CASE("Assets keep their UUIDs across scans, and when they move with their metadata")
	{
		const Testing::TemporaryDirectory directory("AssetMove");
		const std::filesystem::path& root = directory.GetPath();
		WriteText(root / "Brick.png", "png");

		AssetRegistry registry(root);
		REQUIRE(registry.Scan().has_value());
		const UUID id = registry.FindByPath("Brick.png")->Metadata.Id;

		const auto rescan = registry.Scan();
		REQUIRE(rescan.has_value());
		CHECK(rescan->CreatedMetadata.empty());
		CHECK(registry.FindByPath("Brick.png")->Metadata.Id == id);

		std::filesystem::create_directories(root / "Moved");
		std::filesystem::rename(root / "Brick.png", root / "Moved" / "Brick.png");
		std::filesystem::rename(root / "Brick.png.meta", root / "Moved" / "Brick.png.meta");
		REQUIRE(registry.Scan().has_value());
		const AssetInfo* moved = registry.Find(id);
		REQUIRE(moved != nullptr);
		CHECK(moved->Path == "Moved/Brick.png");
		CHECK(registry.FindByPath("Brick.png") == nullptr);
	}

	TEST_CASE("A copied asset with copied metadata gets a new UUID")
	{
		const Testing::TemporaryDirectory directory("AssetCopy");
		const std::filesystem::path& root = directory.GetPath();
		WriteText(root / "A.png", "png");
		AssetRegistry registry(root);
		REQUIRE(registry.Scan().has_value());
		const UUID original = registry.FindByPath("A.png")->Metadata.Id;

		std::filesystem::copy_file(root / "A.png", root / "B.png");
		std::filesystem::copy_file(root / "A.png.meta", root / "B.png.meta");
		const auto report = registry.Scan();
		REQUIRE(report.has_value());

		CHECK(report->ReassignedIds == std::vector<std::string>{"B.png"});
		CHECK(registry.FindByPath("A.png")->Metadata.Id == original);
		const UUID copy = registry.FindByPath("B.png")->Metadata.Id;
		CHECK(copy != original);
		// The new UUID was saved
		const auto saved = AssetMetadataSerializer::Load(root / "B.png.meta");
		REQUIRE(saved.has_value());
		CHECK(saved->Id == copy);
	}

	TEST_CASE("Scanning reports orphaned and invalid metadata without changing it")
	{
		const Testing::TemporaryDirectory directory("AssetProblems");
		const std::filesystem::path& root = directory.GetPath();
		WriteText(root / "Gone.png.meta", "{}");
		WriteText(root / "Broken.png", "png");
		WriteText(root / "Broken.png.meta", "{ not json");
		WriteText(root / "Wrong.png", "png");
		const AssetMetadata wrong{.Id = UUID::Generate(), .Type = AssetType::Audio};
		REQUIRE(AssetMetadataSerializer::Save(wrong, root / "Wrong.png.meta").has_value());

		AssetRegistry registry(root);
		const auto report = registry.Scan();
		REQUIRE(report.has_value());

		CHECK(report->OrphanedMetadata == std::vector<std::string>{"Gone.png.meta"});
		CHECK(report->Errors.size() == 2);
		CHECK(registry.GetAssetCount() == 0);
		// Nothing was overwritten
		const auto broken = ReadFile(root / "Broken.png.meta");
		REQUIRE(broken.has_value());
		CHECK(*broken == "{ not json");
		CHECK(std::filesystem::exists(root / "Gone.png.meta"));
	}

	TEST_CASE("Scanning a directory that doesn't exist fails")
	{
		const Testing::TemporaryDirectory directory("AssetMissing");
		AssetRegistry registry(directory.GetPath() / "Missing");
		const auto report = registry.Scan();
		REQUIRE_FALSE(report.has_value());
		CHECK(report.error().GetCode() == ErrorCode::FileNotFound);
	}

}
