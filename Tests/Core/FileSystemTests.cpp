#include "Lodestone/Core/FileSystem.h"

#include "Common/DescribeError.h"
#include "Common/TemporaryDirectory.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

namespace Lodestone {

	namespace {

		std::string Read(const std::filesystem::path& path, uint64_t maxSize = DefaultMaxFileSize)
		{
			auto text = ReadFile(path, maxSize);
			REQUIRE_MESSAGE(text.has_value(), Testing::DescribeError(text));
			return std::move(*text);
		}

		ErrorCode ReadError(const std::filesystem::path& path, uint64_t maxSize = DefaultMaxFileSize)
		{
			const auto text = ReadFile(path, maxSize);
			REQUIRE_FALSE(text.has_value());
			return text.error().GetCode();
		}

	}

	TEST_CASE("Files are written atomically and read back")
	{
		const Testing::TemporaryDirectory directory("FileSystem");
		const std::filesystem::path path = directory.GetPath() / "File.txt";

		REQUIRE(WriteFileAtomically(path, "first").has_value());
		CHECK(Read(path) == "first");
		REQUIRE(WriteFileAtomically(path, std::string("second\0with a null", 18)).has_value());
		CHECK(Read(path) == std::string("second\0with a null", 18));

		// No temporary files are left behind
		size_t files = 0;
		for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator(directory.GetPath()))
			++files;
		CHECK(files == 1);
	}

	TEST_CASE("Reading a file fails for missing files, directories and files over the limit")
	{
		const Testing::TemporaryDirectory directory("FileSystemErrors");
		const std::filesystem::path path = directory.GetPath() / "File.txt";
		REQUIRE(WriteFileAtomically(path, "0123456789").has_value());

		CHECK(ReadError(directory.GetPath() / "Missing.txt") == ErrorCode::FileNotFound);
		CHECK(ReadError(directory.GetPath()) == ErrorCode::InvalidArgument);
		CHECK(ReadError(path, 9) == ErrorCode::InvalidArgument);
		CHECK(Read(path, 10) == "0123456789");
	}

	TEST_CASE("Writing into a directory that doesn't exist fails")
	{
		const Testing::TemporaryDirectory directory("FileSystemWrite");
		const auto written = WriteFileAtomically(directory.GetPath() / "Missing" / "File.txt", "text");
		REQUIRE_FALSE(written.has_value());
		CHECK(written.error().GetCode() == ErrorCode::IoError);
	}

	TEST_CASE("Paths convert to and from UTF-8 with forward slashes")
	{
		const std::filesystem::path path = PathFromUtf8("Textures/Ünïcødé/Brick.png");
		CHECK(PathToUtf8(path) == "Textures/Ünïcødé/Brick.png");
		CHECK(path.filename() == PathFromUtf8("Brick.png"));
		CHECK(PathToUtf8(std::filesystem::path("A") / "B") == "A/B");
	}

}
