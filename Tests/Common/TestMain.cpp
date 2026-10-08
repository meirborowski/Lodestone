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
			return context.run();
		});
}
