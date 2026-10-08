# Runs LodestoneAssertAbortTest, and checks that the default assert handler wrote the failure report to the standard
# error and ended the process. When asserts are compiled out (Dist), checks that the program ran to completion instead.
#
# Usage: cmake -D EXECUTABLE=<path> [-D ARGUMENTS=<arguments>] -D ASSERTS_ENABLED=<0|1> -P CheckAssertAbort.cmake

execute_process(
	COMMAND "${EXECUTABLE}" ${ARGUMENTS}
	RESULT_VARIABLE result
	OUTPUT_VARIABLE output
	ERROR_VARIABLE error
	TIMEOUT 60
)

if(ASSERTS_ENABLED)
	if(result EQUAL 0)
		message(FATAL_ERROR "The process exited normally after a failed assert\nstdout: ${output}\nstderr: ${error}")
	endif()
	if(NOT error MATCHES "Assertion 'answer == 0' failed at [^\n]*AssertAbortTestMain\\.cpp:[0-9]+: Expected failure 42")
		message(FATAL_ERROR "The standard error doesn't contain the failure report\nstdout: ${output}\nstderr: ${error}")
	endif()
	message(STATUS "The process reported the failed assert and ended (${result})")
else()
	if(NOT result EQUAL 0)
		message(FATAL_ERROR "The process failed (${result}) although asserts are compiled out\nstdout: ${output}\nstderr: ${error}")
	endif()
	message(STATUS "The assert was compiled out")
endif()
