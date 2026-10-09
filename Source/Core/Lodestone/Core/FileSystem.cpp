#include "Lodestone/Core/FileSystem.h"

#include "Lodestone/Core/UUID.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/std.h>

#include <fstream>
#include <system_error>

namespace Lodestone {

	std::expected<std::string, Error> ReadFile(const std::filesystem::path& path, uint64_t maxSize)
	{
		std::error_code error;
		const auto status = std::filesystem::status(path, error);
		if (error || !std::filesystem::exists(status))
			return std::unexpected(Error(ErrorCode::FileNotFound, fmt::format("{} doesn't exist", path)));
		if (!std::filesystem::is_regular_file(status))
			return std::unexpected(Error(ErrorCode::InvalidArgument, fmt::format("{} isn't a file", path)));

		const uint64_t size = std::filesystem::file_size(path, error);
		if (error)
			return std::unexpected(
				Error(ErrorCode::IoError, fmt::format("Can't read the size of {}: {}", path, error)));
		if (size > maxSize)
			return std::unexpected(Error(ErrorCode::InvalidArgument,
				fmt::format("{} is {} bytes, more than the limit of {}", path, size, maxSize)));

		std::ifstream file(path, std::ios::binary);
		if (!file)
			return std::unexpected(Error(ErrorCode::IoError, fmt::format("Can't open {}", path)));
		std::string contents(static_cast<size_t>(size), '\0');
		file.read(contents.data(), static_cast<std::streamsize>(contents.size()));
		// The file may have shrunk since its size was read
		contents.resize(static_cast<size_t>(file.gcount()));
		if (file.bad())
			return std::unexpected(Error(ErrorCode::IoError, fmt::format("Can't read {}", path)));
		return contents;
	}

	std::string PathToUtf8(const std::filesystem::path& path)
	{
		const std::u8string text = path.generic_u8string();
		return {text.begin(), text.end()};
	}

	std::filesystem::path PathFromUtf8(std::string_view text)
	{
		return {std::u8string(text.begin(), text.end())};
	}

	std::expected<void, Error> WriteFileAtomically(const std::filesystem::path& path, std::string_view contents)
	{
		// A unique name, so concurrent writers never share a temporary file
		std::filesystem::path temporary = path;
		temporary += fmt::format(".{}.tmp", UUID::Generate());

		{
			std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
			if (!file)
				return std::unexpected(Error(ErrorCode::IoError, fmt::format("Can't create {}", temporary)));
			file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
			file.close();
			if (!file)
			{
				std::error_code ignored;
				std::filesystem::remove(temporary, ignored);
				return std::unexpected(Error(ErrorCode::IoError, fmt::format("Can't write {}", temporary)));
			}
		}

		std::error_code error;
		std::filesystem::rename(temporary, path, error);
		if (error)
		{
			std::error_code ignored;
			std::filesystem::remove(temporary, ignored);
			return std::unexpected(Error(ErrorCode::IoError, fmt::format("Can't replace {}: {}", path, error)));
		}
		return {};
	}

}
