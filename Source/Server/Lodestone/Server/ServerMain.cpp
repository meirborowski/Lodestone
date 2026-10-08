#include "Lodestone/Core/Log.h"
#include "Lodestone/Core/RunMain.h"
#include "Lodestone/Core/Version.h"

#include <cstdlib>

int main()
{
	return Lodestone::RunMain("Lodestone Server", {},
		[]
		{
			// Hosting a multiplayer game arrives in Milestone 11 (see docs/Milestones.md)
			LS_INFO("Lodestone Server {}", Lodestone::VersionString);
			return EXIT_SUCCESS;
		});
}
