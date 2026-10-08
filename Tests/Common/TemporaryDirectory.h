#pragma once

#include <filesystem>
#include <string_view>

namespace Lodestone::Testing {

	// A uniquely named directory in the system's temporary directory, for a test's files. Removed with its contents
	// when the object is destroyed
	class TemporaryDirectory
	{
	public:
		explicit TemporaryDirectory(std::string_view name);
		~TemporaryDirectory();

		TemporaryDirectory(const TemporaryDirectory&) = delete;
		TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
		TemporaryDirectory(TemporaryDirectory&&) = delete;
		TemporaryDirectory& operator=(TemporaryDirectory&&) = delete;

		const std::filesystem::path& GetPath() const { return m_Path; }

	private:
		std::filesystem::path m_Path;
	};

}
