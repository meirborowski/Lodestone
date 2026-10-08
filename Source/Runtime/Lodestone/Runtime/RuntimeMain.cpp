#include "Lodestone/Core/Log.h"
#include "Lodestone/Core/RunMain.h"
#include "Lodestone/Core/Version.h"

#include <cstdlib>

int main()
{
	return Lodestone::RunMain("Lodestone Runtime", {},
		[]
		{
			// Loading and running an exported game arrives in Milestone 12 (see docs/Milestones.md)
			LS_INFO("Lodestone Runtime {}", Lodestone::VersionString);
			return EXIT_SUCCESS;
		});
}
