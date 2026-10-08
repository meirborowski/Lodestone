#include "Lodestone/Core/Log.h"
#include "Lodestone/Core/RunMain.h"
#include "Lodestone/Core/Version.h"

#include <cstdlib>

#if defined(LS_CONFIG_DIST)
	#error "The editor isn't built in Dist: exported games must not contain editor code"
#endif

int main()
{
	return Lodestone::RunMain("Lodestone Editor", {},
		[]
		{
			// The editor UI, MCP server and headless mode arrive in Milestone 4 (see docs/Milestones.md)
			LS_INFO("Lodestone Editor {}", Lodestone::VersionString);
			return EXIT_SUCCESS;
		});
}
