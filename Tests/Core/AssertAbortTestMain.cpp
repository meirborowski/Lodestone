#include "Lodestone/Core/Assert.h"
#include "Lodestone/Core/Log.h"

#include <cstdio>
#include <cstdlib>
#include <string_view>

// Fails an assert with the default handler installed, which must report the failure on the standard error and abort.
// CheckAssertAbort.cmake runs this program and checks the outcome. With --log-without-console the log is initialized
// without a console sink first, so the report must reach the standard error despite the log being active
int main(int argc, char** argv)
{
	const bool logWithoutConsole = argc > 1 && std::string_view(argv[1]) == "--log-without-console";
	if (logWithoutConsole && !Lodestone::Log::Init({.Console = Lodestone::LogConsole::None}))
		return EXIT_FAILURE;

	const int answer = 42;
	LS_CORE_ASSERT(answer == 0, "Expected failure {}", answer);

	// Only reached when asserts are compiled out (Dist)
	std::puts("The assert was compiled out");
	return EXIT_SUCCESS;
}
