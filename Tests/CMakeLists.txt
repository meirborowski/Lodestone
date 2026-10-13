# Tests are grouped into tiers with CTest labels (see docs/Testing.md#test-tiers):
#   unit        - fast, no GPU or network; run on every change
#   render      - rendering tests, including reference-image tests
#   integration - MCP end-to-end, networking over loopback, and export
#   fuzz        - fuzz tests, run for a fixed time budget (LS_FUZZ_SECONDS)
#
# Each test executable links only what it tests: LodestoneCore tests run without a window, GPU or audio device.

# Helpers shared by the test executables
add_library(LodestoneTestSupport STATIC
	Common/LogCapture.cpp
	Common/LogCapture.h
	Common/LogLevelScope.cpp
	Common/LogLevelScope.h
	Common/DescribeError.h
	Common/Fuzzer.cpp
	Common/Fuzzer.h
	Common/LogShutdownScope.cpp
	Common/LogShutdownScope.h
	Common/ReferenceImage.cpp
	Common/ReferenceImage.h
	Common/TemporaryDirectory.cpp
	Common/TemporaryDirectory.h
)
target_include_directories(LodestoneTestSupport PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(LodestoneTestSupport PUBLIC LodestoneCore doctest::doctest)
target_compile_definitions(LodestoneTestSupport
	PUBLIC
		# doctest includes the standard headers instead of forward-declaring std types, which MSVC's library rejects
		# when values such as std::string_view are printed in assertion messages
		DOCTEST_CONFIG_USE_STD_HEADERS
		# Files that tests load: documents in older format versions, scenes and asset metadata
		LS_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Fixtures"
	PRIVATE
		# Reference images are stored with Git LFS; failed comparisons are written to the build tree, which CI uploads
		LS_REFERENCE_IMAGE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/ReferenceImages"
		LS_RENDER_OUTPUT_DIR="${PROJECT_BINARY_DIR}/RenderOutput"
		LS_FUZZ_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Fuzz/Corpus"
)
ls_configure_target(LodestoneTestSupport)

# ls_add_test_executable(<target> PREFIX <prefix> LABELS <tier>... SOURCES <file>...
#                        [LIBRARIES <library>...] [EXCLUDE_SUITE <doctest test suite>])
#
# Adds a doctest executable, with the standard test runner (Common/TestMain.cpp), and registers each of its test cases
# with CTest as "<prefix>.<test case name>". The test cases of EXCLUDE_SUITE are left out of the executable's runs, and
# so aren't registered. (doctest_discover_tests' TEST_SPEC can't do that: it only matches test case names.)
function(ls_add_test_executable target)
	cmake_parse_arguments(PARSE_ARGV 1 ARG "" "PREFIX;EXCLUDE_SUITE" "LABELS;SOURCES;LIBRARIES")
	if(NOT ARG_PREFIX OR NOT ARG_LABELS OR NOT ARG_SOURCES)
		message(FATAL_ERROR "ls_add_test_executable(${target}) needs PREFIX, LABELS and SOURCES")
	endif()

	add_executable(${target} Common/TestMain.cpp ${ARG_SOURCES})
	target_link_libraries(${target} PRIVATE LodestoneTestSupport ${ARG_LIBRARIES})
	if(ARG_EXCLUDE_SUITE)
		target_compile_definitions(${target} PRIVATE LS_TEST_SUITE_EXCLUDE="${ARG_EXCLUDE_SUITE}")
	endif()
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
		Core/AnimationComponentsTests.cpp
		Core/AnimationTests.cpp
		Core/AssertTests.cpp
		Core/AssetTests.cpp
		Core/Base64Tests.cpp
		Core/BaseTests.cpp
		Core/BoundingBoxTests.cpp
		Core/CommandLineTests.cpp
		Core/ComponentRegistryTests.cpp
		Core/EntitySerializerTests.cpp
		Core/EnvironmentTests.cpp
		Core/ErrorTests.cpp
		Core/FileFormatTests.cpp
		Core/FileSystemTests.cpp
		Core/HdrImageTests.cpp
		Core/ImageTests.cpp
		Core/InputCodesTests.cpp
		Core/JsonTests.cpp
		Core/LanguageSupportTests.cpp
		Core/LogTests.cpp
		Core/ModelCacheTests.cpp
		Core/ModelEntitiesTests.cpp
		Core/ModelImporterTests.cpp
		Core/PrefabSerializerTests.cpp
		Core/ProjectTests.cpp
		Core/RandomGeneratorTests.cpp
		Core/ReferenceImageTests.cpp
		Core/RenderComponentsTests.cpp
		Core/RunMainTests.cpp
		Core/ScenePoseTests.cpp
		Core/SceneSerializerTests.cpp
		Core/SceneTests.cpp
		Core/SimulationTests.cpp
		Core/UUIDTests.cpp
		Scripting/LuaSmokeTests.cpp
	LIBRARIES
		Lodestone::Lua
)

# LodestoneClient code that needs no window or GPU: input, and the renderer's CPU side
ls_add_test_executable(LodestoneClientTests
	PREFIX Client
	LABELS unit
	SOURCES
		Client/DeviceInputTests.cpp
		Client/ImageBasedLightingTests.cpp
		Client/InputCommandBuilderTests.cpp
		Client/MipChainTests.cpp
		Client/RenderWorldTests.cpp
		Client/ShadowCascadeTests.cpp
	LIBRARIES
		LodestoneClient
)

# Rendering tests, on lavapipe wherever it's available (see cmake/Lavapipe.cmake and Render/RenderTestDevice.h).
# Without it, only the tests that don't compare against reference images are registered - on macOS, they check that
# MoltenVK provides a Vulkan device and renders
if(LS_LAVAPIPE_DRIVER)
	set(LS_RENDER_EXCLUDED_SUITE "")
else()
	set(LS_RENDER_EXCLUDED_SUITE ReferenceImages)
endif()
ls_add_test_executable(LodestoneRenderTests
	PREFIX Render
	LABELS render
	SOURCES
		Render/GraphicsDeviceTests.cpp
		Render/ImGuiRenderTests.cpp
		Render/LineRendererTests.cpp
		Render/RenderTestDevice.cpp
		Render/RenderTestDevice.h
		Render/SceneRendererTests.cpp
		Render/TriangleRenderTests.cpp
	LIBRARIES
		LodestoneClient
	EXCLUDE_SUITE ${LS_RENDER_EXCLUDED_SUITE}
)
if(LS_LAVAPIPE_DRIVER)
	target_compile_definitions(LodestoneRenderTests PRIVATE LS_LAVAPIPE_DRIVER="${LS_LAVAPIPE_DRIVER}")
else()
	# Skipped rather than silently missing, so the reason shows in every test run
	add_test(NAME "Render.Reference-image tests"
		COMMAND "${CMAKE_COMMAND}" -E echo "Skipped: ${LS_LAVAPIPE_UNAVAILABLE_REASON}"
	)
	set_tests_properties("Render.Reference-image tests" PROPERTIES
		LABELS render
		SKIP_REGULAR_EXPRESSION "Skipped:"
	)
endif()

# The editor: commands, the editor context, the scene view's picking, the UI (without a GPU) and every MCP tool,
# called in-process. Not built in Dist, like the editor
if(TARGET LodestoneEditorLib)
	ls_add_test_executable(LodestoneEditorTests
		PREFIX Editor
		LABELS unit
		SOURCES
			Editor/CommandHistoryTests.cpp
			Editor/EditorContextTests.cpp
			Editor/EditorSupportTests.cpp
			Editor/EditorToolsTests.cpp
			Editor/EditorUITests.cpp
			Editor/EntityCommandsTests.cpp
			Editor/McpServerTests.cpp
			Editor/McpTransportTests.cpp
			Editor/SceneViewTests.cpp
		LIBRARIES
			LodestoneEditorLib
	)

	# MCP over real connections: the HTTP transport on a loopback port, in-process; and the editor executable
	# driven over stdio and HTTP by a separate client, as an agent would - including the agent smoke test that builds a
	# scene with MCP tools alone (Milestone 4's acceptance test)
	ls_add_test_executable(LodestoneEditorIntegrationTests
		PREFIX Editor
		LABELS integration
		SOURCES
			Editor/HttpTransportTests.cpp
		LIBRARIES
			LodestoneEditorLib
			httplib
	)

	find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
	foreach(transport IN ITEMS stdio http)
		set(name "Editor.Agent smoke test over ${transport}")
		set(arguments --editor "$<TARGET_FILE:LodestoneEditor>" --transport ${transport}
			--model "${CMAKE_CURRENT_SOURCE_DIR}/Fixtures/Models/Primitives.glb"
			--animated-model "${CMAKE_CURRENT_SOURCE_DIR}/Fixtures/Models/SkinnedArm.glb")
		if(LS_LAVAPIPE_DRIVER)
			list(APPEND arguments --vulkan-driver "${LS_LAVAPIPE_DRIVER}")
		endif()
		set(configurations "")
		if(LS_MULTI_CONFIG)
			set(configurations CONFIGURATIONS Debug Release)
		endif()
		add_test(NAME "${name}"
			COMMAND Python3::Interpreter -I "${CMAKE_CURRENT_SOURCE_DIR}/Editor/McpAgentSmokeTest.py" ${arguments}
			${configurations}
		)
		set_tests_properties("${name}" PROPERTIES LABELS integration TIMEOUT 300)
	endforeach()

	# MCP messages come from clients the editor doesn't control
	ls_add_test_executable(LodestoneEditorFuzzTests
		PREFIX Fuzz
		LABELS fuzz
		SOURCES
			Fuzz/McpFuzzTests.cpp
		LIBRARIES
			LodestoneEditorLib
	)

	if(LS_MULTI_CONFIG)
		set_target_properties(LodestoneEditorTests LodestoneEditorIntegrationTests LodestoneEditorFuzzTests PROPERTIES
			EXCLUDE_FROM_ALL "$<CONFIG:Dist>")
	endif()
endif()

# The renderer's performance benchmark (see docs/Testing.md#performance). Run it locally on a GPU to measure frame
# times against the baseline; CI runs a short version on lavapipe to check that draw calls haven't risen
add_executable(LodestoneBenchmark
	Benchmark/BenchmarkMain.cpp
	Benchmark/BenchmarkScene.cpp
	Benchmark/BenchmarkScene.h
)
target_include_directories(LodestoneBenchmark PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(LodestoneBenchmark PRIVATE LodestoneClient)
target_compile_definitions(LodestoneBenchmark PRIVATE LS_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Fixtures")
ls_configure_target(LodestoneBenchmark)

set(LS_BENCHMARK_ARGUMENTS --baseline "${CMAKE_CURRENT_SOURCE_DIR}/Benchmark/Baseline.json" --frames 8 --size 320x180)
if(LS_LAVAPIPE_DRIVER)
	list(APPEND LS_BENCHMARK_ARGUMENTS --vulkan-driver "${LS_LAVAPIPE_DRIVER}")
endif()
add_test(NAME "Render.Benchmark draw calls stay within the baseline"
	COMMAND LodestoneBenchmark ${LS_BENCHMARK_ARGUMENTS}
)
set_tests_properties("Render.Benchmark draw calls stay within the baseline" PROPERTIES LABELS render TIMEOUT 600)

# Fuzz tests for everything that parses untrusted input (see Common/Fuzzer.h and docs/Testing.md#tests)
ls_add_test_executable(LodestoneFuzzTests
	PREFIX Fuzz
	LABELS fuzz
	SOURCES
		Fuzz/LoaderFuzzTests.cpp
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

# Every executable starts, reports its version and exits cleanly - without opening a window, so it runs anywhere
foreach(app IN ITEMS LodestoneRuntime LodestoneServer LodestoneEditor)
	if(NOT TARGET ${app})
		continue()
	endif()
	set(configurations "")
	if(app STREQUAL "LodestoneEditor" AND LS_MULTI_CONFIG)
		# A multi-config build tree has the editor target, but doesn't build it in Dist
		set(configurations CONFIGURATIONS Debug Release)
	endif()
	string(REPLACE "Lodestone" "Lodestone " displayName "${app}")
	set(name "Apps.${app} reports its version")
	add_test(NAME "${name}" COMMAND ${app} --version ${configurations})
	set_tests_properties("${name}" PROPERTIES
		LABELS unit
		PASS_REGULAR_EXPRESSION "^${displayName} ${PROJECT_VERSION}"
	)
endforeach()
