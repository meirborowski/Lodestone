#include "Common/DescribeError.h"
#include "Common/Fuzzer.h"
#include "Lodestone/Asset/AssetMetadata.h"
#include "Lodestone/Scene/SceneSerializer.h"

#include <doctest/doctest.h>
#include <spdlog/fmt/fmt.h>

#include <initializer_list>
#include <string>
#include <vector>

// Fuzz tests for the loaders of files that may come from anywhere: scenes and asset metadata. Corrupt or hostile input
// must fail cleanly - never crash, hang or trip a sanitizer. Whatever does load must also save and load again
// unchanged, which catches loaders that accept input their own writers can't represent

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

}
