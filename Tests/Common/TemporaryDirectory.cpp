#include "Common/TemporaryDirectory.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/std.h>

#include <cstdio>
#include <exception>
#include <random>
#include <system_error>

namespace Lodestone::Testing {

	TemporaryDirectory::TemporaryDirectory(std::string_view name)
		: m_Path(std::filesystem::temp_directory_path() /
			  fmt::format("LodestoneTests-{}-{:08x}", name, std::random_device()()))
	{
		std::filesystem::create_directories(m_Path);
	}

	TemporaryDirectory::~TemporaryDirectory()
	{
		// Removal is best effort: a directory left behind in the temporary directory mustn't fail a test
		try
		{
			std::error_code error;
			std::filesystem::remove_all(m_Path, error);
			if (error)
				fmt::print(stderr, "Can't remove the temporary directory {}: {}\n", m_Path, error.message());
		}
		catch (const std::exception& exception)
		{
			std::fputs(exception.what(), stderr);
			std::fputc('\n', stderr);
		}
	}

}
