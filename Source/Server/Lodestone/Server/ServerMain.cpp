#include "Lodestone/Core/CommandLine.h"
#include "Lodestone/Core/Log.h"
#include "Lodestone/Core/RunMain.h"
#include "Lodestone/Core/Version.h"

#include <spdlog/fmt/fmt.h>

#include <array>
#include <cstdlib>

int main(int argc, char** argv)
{
	return Lodestone::RunMain("Lodestone Server", {},
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
				fmt::print("Lodestone Server {}\n", Lodestone::VersionString);
				return EXIT_SUCCESS;
			}

			// Hosting a multiplayer game arrives in Milestone 11 (see docs/Milestones.md)
			LS_INFO("Lodestone Server {}", Lodestone::VersionString);
			return EXIT_SUCCESS;
		});
}
