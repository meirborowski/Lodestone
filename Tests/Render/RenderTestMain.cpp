#define DOCTEST_CONFIG_IMPLEMENT
#include "Lodestone/Core/Environment.h"
#include "Lodestone/Core/Log.h"
#include "Lodestone/Core/RunMain.h"

#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <string>

int main(int argc, char** argv)
{
	return Lodestone::RunMain("Lodestone render tests", {},
		[argc, argv]
		{
#if defined(LS_LAVAPIPE_ICD)
			// Rendering tests always render with lavapipe at the pinned Mesa version, so results don't depend on the
			// GPU or driver - however the tests are started. The Vulkan loader reads these variables when Vulkan
			// starts. The Windows loader only accepts the manifest path with native separators
			const std::string manifest = std::filesystem::path(LS_LAVAPIPE_ICD).make_preferred().string();
			for (const char* variable : {"VK_DRIVER_FILES", "VK_ICD_FILENAMES"})
			{
				if (const auto written = Lodestone::WriteEnvironmentVariable(variable, manifest); !written)
				{
					LS_CRITICAL("Can't select lavapipe: {}", written.error());
					return EXIT_FAILURE;
				}
			}
#endif
			doctest::Context context(argc, argv);
			return context.run();
		});
}
