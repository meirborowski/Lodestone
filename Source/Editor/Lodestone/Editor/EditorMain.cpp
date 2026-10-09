#include "Lodestone/Core/CommandLine.h"
#include "Lodestone/Core/Log.h"
#include "Lodestone/Core/RunMain.h"
#include "Lodestone/Core/Version.h"

#include <spdlog/fmt/fmt.h>

#include <array>
#include <cstdlib>

#if defined(LS_CONFIG_DIST)
	#error "The editor isn't built in Dist: exported games must not contain editor code"
#endif

int main(int argc, char** argv)
{
	return Lodestone::RunMain("Lodestone Editor", {},
		[argc, argv]
		{
			const Lodestone::CommandLine commandLine(argc, argv);
			constexpr std::array<std::string_view, 1> flags = {"--version"};
			if (auto valid = commandLine.Validate(flags, {}); !valid)
			{
				LS_CRITICAL("{}", valid.error());
				return EXIT_FAILURE;
			}
			if (commandLine.HasFlag("--version"))
			{
				fmt::print("Lodestone Editor {}\n", Lodestone::VersionString);
				return EXIT_SUCCESS;
			}

			// The editor UI, MCP server and headless mode arrive in Milestone 4 (see docs/Milestones.md)
			LS_INFO("Lodestone Editor {}", Lodestone::VersionString);
			return EXIT_SUCCESS;
		});
}
