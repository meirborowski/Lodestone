#define DOCTEST_CONFIG_IMPLEMENT
#include "Lodestone/Core/RunMain.h"

#include <doctest/doctest.h>

int main(int argc, char** argv)
{
	// Engine code under test logs as usual; CTest shows the output of failing tests
	return Lodestone::RunMain("Lodestone tests", {},
		[argc, argv]
		{
			doctest::Context context(argc, argv);
#if defined(LS_TEST_SUITE_EXCLUDE)
			// A suite this build can't run (see ls_add_test_executable()), left out of runs and listings alike - so
			// CTest's test discovery doesn't register it either
			context.addFilter("test-suite-exclude", LS_TEST_SUITE_EXCLUDE);
#endif
			return context.run();
		});
}
