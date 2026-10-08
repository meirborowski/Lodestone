# Tests are grouped into tiers with CTest labels (see docs/Testing.md#test-tiers):
#   unit        - fast, no GPU or network; run on every change
#   render      - reference-image tests
#   integration - MCP end-to-end, networking over loopback, and export
#   fuzz        - fuzz tests, run for a fixed time budget
#
# Each test executable links only what it tests: LodestoneCore tests run without a window, GPU or audio device.

# Helpers shared by the test executables
add_library(LodestoneTestSupport STATIC
	Common/LogCapture.cpp
	Common/LogCapture.h
	Common/LogLevelScope.cpp
	Common/LogLevelScope.h
	Common/LogShutdownScope.cpp
	Common/LogShutdownScope.h
	Common/TemporaryDirectory.cpp
	Common/TemporaryDirectory.h
)
target_include_directories(LodestoneTestSupport PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(LodestoneTestSupport PUBLIC LodestoneCore doctest::doctest)
target_compile_definitions(LodestoneTestSupport PUBLIC
	# doctest includes the standard headers instead of forward-declaring std types, which MSVC's library rejects when
	# values such as std::string_view are printed in assertion messages
	DOCTEST_CONFIG_USE_STD_HEADERS
)
ls_configure_target(LodestoneTestSupport)

# ls_add_test_executable(<target> PREFIX <prefix> LABELS <tier>... SOURCES <file>... [LIBRARIES <library>...])
#
# Adds a doctest executable and registers each of its test cases with CTest as "<prefix>.<test case name>"
function(ls_add_test_executable target)
	cmake_parse_arguments(PARSE_ARGV 1 ARG "" "PREFIX" "LABELS;SOURCES;LIBRARIES")
	if(NOT ARG_PREFIX OR NOT ARG_LABELS OR NOT ARG_SOURCES)
		message(FATAL_ERROR "ls_add_test_executable(${target}) needs PREFIX, LABELS and SOURCES")
	endif()

	add_executable(${target} Common/TestMain.cpp ${ARG_SOURCES})
	target_link_libraries(${target} PRIVATE LodestoneTestSupport ${ARG_LIBRARIES})
	ls_configure_target(${target})

	doctest_discover_tests(${target}
		TEST_PREFIX "${ARG_PREFIX}."
		PROPERTIES LABELS "${ARG_LABELS}"
	)
endfunction()

ls_add_test_executable(LodestoneCoreTests
	PREFIX Core
	LABELS unit
	SOURCES
		Core/AssertTests.cpp
		Core/BaseTests.cpp
		Core/ErrorTests.cpp
		Core/LanguageSupportTests.cpp
		Core/LogTests.cpp
		Core/RunMainTests.cpp
		Scripting/LuaSmokeTests.cpp
	LIBRARIES
		Lodestone::Lua
)

# The default assert handler ends the process, so it's tested from a separate executable
add_executable(LodestoneAssertAbortTest Core/AssertAbortTestMain.cpp)
target_link_libraries(LodestoneAssertAbortTest PRIVATE LodestoneCore)
ls_configure_target(LodestoneAssertAbortTest)
foreach(variant IN ITEMS "without a log" "with a log that has no console")
	if(variant STREQUAL "without a log")
		set(arguments "")
	else()
		set(arguments "--log-without-console")
	endif()
	set(name "Core.Default assert handler reports the failure and aborts, ${variant}")
	add_test(NAME "${name}"
		COMMAND "${CMAKE_COMMAND}"
			-D "EXECUTABLE=$<TARGET_FILE:LodestoneAssertAbortTest>"
			-D "ARGUMENTS=${arguments}"
			-D "ASSERTS_ENABLED=$<NOT:$<CONFIG:Dist>>"
			-P "${CMAKE_CURRENT_SOURCE_DIR}/Core/CheckAssertAbort.cmake"
	)
	set_tests_properties("${name}" PROPERTIES LABELS unit)
endforeach()

# Every executable starts and exits cleanly
foreach(app IN ITEMS LodestoneRuntime LodestoneServer LodestoneEditor)
	if(NOT TARGET ${app})
		continue()
	endif()
	set(configurations "")
	if(app STREQUAL "LodestoneEditor" AND LS_MULTI_CONFIG)
		# A multi-config build tree has the editor target, but doesn't build it in Dist
		set(configurations CONFIGURATIONS Debug Release)
	endif()
	add_test(NAME "Apps.${app} starts and exits cleanly" COMMAND ${app} ${configurations})
	set_tests_properties("Apps.${app} starts and exits cleanly" PROPERTIES LABELS unit)
endforeach()
