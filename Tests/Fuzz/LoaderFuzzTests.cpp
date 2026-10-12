#include "Common/DescribeError.h"
#include "Common/Fuzzer.h"
#include "Lodestone/Asset/AssetMetadata.h"
#include "Lodestone/Asset/ModelImporter.h"
#include "Lodestone/Core/HdrImage.h"
#include "Lodestone/Core/Image.h"
#include "Lodestone/Project/Project.h"
#include "Lodestone/Scene/EntitySerializer.h"
#include "Lodestone/Scene/PrefabSerializer.h"
#include "Lodestone/Scene/SceneSerializer.h"

#include <doctest/doctest.h>
#include <spdlog/fmt/fmt.h>

#include <initializer_list>
#include <string>
#include <vector>

// Fuzz tests for the loaders of files that may come from anywhere: scenes, prefabs, projects, asset metadata, and the
// importers of models, images and HDR environment maps.
// Corrupt or hostile input must fail cleanly - never crash, hang or trip a sanitizer. Whatever does load must also save
// and load again unchanged, which catches loaders that accept input their own writers can't represent

namespace Lodestone {

	namespace {

		std::vector<std::string> MakeJsonDictionary(std::initializer_list<const char*> words)
		{
			std::vector<std::string> dictionary = {"true", "false", "null", "{}", "[]", "\"\"", "1e999", "-0", "0.5",
				"4294967296", "-2147483649", "\"00000000-0000-0000-0000-000000000000\"",
				"\"11111111-1111-4111-8111-111111111111\"", "\\u0000", "\\ud800", "[[[[[[[[", "]]]]]]]]"};
			for (const char* word : words)
				dictionary.push_back(fmt::format("\"{}\"", word));
			return dictionary;
		}

	}

	TEST_CASE("Fuzz the scene loader")
	{
		Testing::FuzzOptions options = Testing::MakeFuzzOptions("Scene");
		options.Dictionary = MakeJsonDictionary({"format", "version", "Lodestone.Scene", "entities", "components", "ID",
			"Name", "Transform", "Hierarchy", "Parent", "Position", "Rotation", "Scale", "PreviousTransform"});

		const auto report = Testing::RunFuzzer(
			[](std::string_view input)
			{
				const auto scene = SceneSerializer::DeserializeFromText(input);
				if (!scene)
					return;
				const std::string saved = SceneSerializer::SerializeToText(**scene);
				const auto reloaded = SceneSerializer::DeserializeFromText(saved);
				REQUIRE_MESSAGE(reloaded.has_value(), Testing::DescribeError(reloaded));
				REQUIRE(SceneSerializer::SerializeToText(**reloaded) == saved);
			},
			options);
		CHECK(report.Runs > 0);
	}

	TEST_CASE("Fuzz the asset metadata loader")
	{
		Testing::FuzzOptions options = Testing::MakeFuzzOptions("AssetMetadata");
		options.Dictionary = MakeJsonDictionary(
			{"format", "version", "Lodestone.AssetMetadata", "id", "type", "importSettings", "Texture", "Scene"});

		const auto report = Testing::RunFuzzer(
			[](std::string_view input)
			{
				const auto metadata = AssetMetadataSerializer::DeserializeFromText(input);
				if (!metadata)
					return;
				const auto reloaded =
					AssetMetadataSerializer::DeserializeFromText(AssetMetadataSerializer::SerializeToText(*metadata));
				REQUIRE_MESSAGE(reloaded.has_value(), Testing::DescribeError(reloaded));
				REQUIRE(*reloaded == *metadata);
			},
			options);
		CHECK(report.Runs > 0);
	}

	TEST_CASE("Fuzz the prefab loader")
	{
		Testing::FuzzOptions options = Testing::MakeFuzzOptions("Prefab");
		options.Dictionary =
			MakeJsonDictionary({"format", "version", "Lodestone.Prefab", "entities", "components", "ID", "Name",
				"Transform", "Hierarchy", "Parent", "Position", "Rotation", "Scale", "PrefabInstance", "Prefab"});

		const auto report = Testing::RunFuzzer(
			[](std::string_view input)
			{
				const auto entities = PrefabSerializer::DeserializeFromText(input);
				if (!entities)
					return;
				// A prefab that loads always instances, and saves as a prefab that loads to the same entities
				Scene scene;
				const auto root = EntitySerializer::InstantiateTree(scene, *entities, EntitySerializer::IdPolicy::Keep);
				REQUIRE_MESSAGE(root.has_value(), Testing::DescribeError(root));
				const std::string saved = PrefabSerializer::SerializeToText(scene, *root);
				const auto reloaded = PrefabSerializer::DeserializeFromText(saved);
				REQUIRE_MESSAGE(reloaded.has_value(), Testing::DescribeError(reloaded));
				REQUIRE(*reloaded == PrefabSerializer::Serialize(scene, *root)["entities"]);
				const auto instance = PrefabSerializer::Instantiate(scene, *reloaded, UUID());
				REQUIRE_MESSAGE(instance.has_value(), Testing::DescribeError(instance));
			},
			options);
		CHECK(report.Runs > 0);
	}

	TEST_CASE("Fuzz the project loader")
	{
		Testing::FuzzOptions options = Testing::MakeFuzzOptions("Project");
		options.Dictionary =
			MakeJsonDictionary({"format", "version", "Lodestone.Project", "name", "startupScene", "tickRate"});

		const auto report = Testing::RunFuzzer(
			[](std::string_view input)
			{
				const auto settings = Project::DeserializeFromText(input);
				if (!settings)
					return;
				const auto reloaded = Project::DeserializeFromText(Project::SerializeToText(*settings));
				REQUIRE_MESSAGE(reloaded.has_value(), Testing::DescribeError(reloaded));
				REQUIRE(*reloaded == *settings);
			},
			options);
		CHECK(report.Runs > 0);
	}

	TEST_CASE("Fuzz the glTF importer")
	{
		Testing::FuzzOptions options = Testing::MakeFuzzOptions("Model");
		options.Dictionary = MakeJsonDictionary({"asset", "version", "2.0", "buffers", "bufferViews", "accessors",
			"meshes", "primitives", "attributes", "POSITION", "NORMAL", "TANGENT", "TEXCOORD_0", "indices", "mode",
			"material", "materials", "pbrMetallicRoughness", "baseColorTexture", "textures", "images", "samplers",
			"nodes", "children", "matrix", "rotation", "scale", "translation", "scenes", "scene", "byteOffset",
			"byteLength", "byteStride", "componentType", "count", "type", "VEC3", "SCALAR", "alphaMode", "MASK",
			"extensionsRequired", "uri", "data:application/octet-stream;base64,", "glTF", "JSON", "BIN"});

		const auto report = Testing::RunFuzzer(
			[](std::string_view input)
			{
				const std::vector<uint8_t> bytes(input.begin(), input.end());
				const auto model = ModelImporter::Import(bytes);
				if (!model)
					return;
				// Whatever imports is consistent: every index refers to a vertex, every reference to what exists
				for (const ModelMesh& mesh : model->Meshes)
				{
					for (const uint32_t index : mesh.Indices)
						REQUIRE(index < mesh.Vertices.size());
					for (const MeshPrimitive& primitive : mesh.Primitives)
					{
						REQUIRE(
							static_cast<uint64_t>(primitive.FirstIndex) + primitive.IndexCount <= mesh.Indices.size());
						REQUIRE(primitive.Material < model->Materials.size());
					}
				}
				for (const ModelTexture& texture : model->Textures)
					REQUIRE(texture.Image < model->Images.size());
				for (const ModelNode& node : model->Nodes)
				{
					REQUIRE((!node.Mesh || *node.Mesh < model->Meshes.size()));
					for (const uint32_t child : node.Children)
						REQUIRE(child < model->Nodes.size());
				}
				static_cast<void>(model->GetBounds());
			},
			options);
		CHECK(report.Runs > 0);
	}

	TEST_CASE("Fuzz the image decoder")
	{
		Testing::FuzzOptions options = Testing::MakeFuzzOptions("Image");
		options.Dictionary = {"\x89PNG\r\n\x1a\n", "IHDR", "IDAT", "IEND", "PLTE", "tRNS", "\xFF\xD8", "\xFF\xC0",
			"\xFF\xC4", "\xFF\xDA", "\xFF\xDB", "\xFF\xD9", "\xFF\xC2"};

		const auto report = Testing::RunFuzzer(
			[](std::string_view input)
			{
				const std::vector<uint8_t> bytes(input.begin(), input.end());
				const auto image = Image::Decode(bytes);
				if (!image)
					return;
				REQUIRE(static_cast<uint64_t>(image->GetWidth()) * image->GetHeight() <= Image::MaxPixelCount);
				REQUIRE(image->GetPixels().size() == static_cast<size_t>(image->GetWidth()) * image->GetHeight() * 4);
			},
			options);
		CHECK(report.Runs > 0);
	}

	TEST_CASE("Fuzz the HDR image decoder")
	{
		Testing::FuzzOptions options = Testing::MakeFuzzOptions("Hdr");
		options.Dictionary = {"#?RADIANCE\n", "#?RGBE\n", "FORMAT=32-bit_rle_rgbe\n", "-Y ", "+X ", "+Y ", "-X ",
			"\x02\x02", "65536", "0"};

		const auto report = Testing::RunFuzzer(
			[](std::string_view input)
			{
				const std::vector<uint8_t> bytes(input.begin(), input.end());
				const auto image = HdrImage::Decode(bytes);
				if (!image)
					return;
				REQUIRE(static_cast<uint64_t>(image->GetWidth()) * image->GetHeight() <= HdrImage::MaxPixelCount);
				// What decodes encodes again
				REQUIRE(image->EncodeHdr().has_value());
			},
			options);
		CHECK(report.Runs > 0);
	}

}
