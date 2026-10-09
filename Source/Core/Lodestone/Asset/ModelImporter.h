#pragma once

#include "Lodestone/Asset/Model.h"
#include "Lodestone/Core/Error.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace Lodestone {

	// Imports glTF 2.0 models - .gltf files with their buffers and images in separate files or embedded as data URIs,
	// and binary .glb files - into a Model. Models are untrusted input: limits keep hostile files from exhausting
	// memory, and a model can only read files in its own directory or below it (see
	// docs/Decisions/0019-model-import.md)
	class ModelImporter
	{
	public:
		static constexpr uint64_t MaxFileSize = 512ull * 1024 * 1024;
		static constexpr uint64_t MaxVertexCount = 16ull * 1024 * 1024;
		static constexpr uint64_t MaxIndexCount = 3 * MaxVertexCount;
		static constexpr uint64_t MaxNodeCount = 65536;

		// Reads a file a model refers to, by its URI (already percent-decoded), relative to the model
		using FileReader = std::function<std::expected<std::vector<uint8_t>, Error>(std::string_view uri)>;

		// Imports a model file, reading the files it refers to from its directory
		[[nodiscard]] static std::expected<Model, Error> Import(const std::filesystem::path& path);
		// Imports a model from memory. Without a reader, the model can't refer to other files
		[[nodiscard]] static std::expected<Model, Error> Import(
			std::span<const uint8_t> data, const FileReader& readFile = {});

		// Reads files from a directory, refusing any path that would leave it
		static FileReader MakeDirectoryReader(const std::filesystem::path& directory);
	};

}
