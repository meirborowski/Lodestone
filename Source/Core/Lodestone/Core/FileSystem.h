#pragma once

#include "Lodestone/Core/Error.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace Lodestone {

	// The largest file ReadFile() reads unless told otherwise: far more than any scene or metadata file needs, and
	// small enough that a corrupt or hostile file can't exhaust memory
	constexpr uint64_t DefaultMaxFileSize = 256ull * 1024 * 1024;

	// Reads a whole file into memory. Fails with FileNotFound if it doesn't exist, InvalidArgument if it's larger than
	// maxSize, and IoError if it can't be read
	[[nodiscard]] std::expected<std::string, Error> ReadFile(
		const std::filesystem::path& path, uint64_t maxSize = DefaultMaxFileSize);

	// A path as UTF-8 text, with forward slashes, the same on every platform - for files, logs and maps keyed by path.
	// Unlike path::string(), it never throws or depends on the system's code page
	std::string PathToUtf8(const std::filesystem::path& path);
	std::filesystem::path PathFromUtf8(std::string_view text);

	// Writes a file, replacing any existing one. The contents go to a temporary file next to it first, which then
	// replaces the target, so a crash or a full disk never leaves a half-written file behind
	[[nodiscard]] std::expected<void, Error> WriteFileAtomically(
		const std::filesystem::path& path, std::string_view contents);

}
