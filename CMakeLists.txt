cmake_minimum_required(VERSION 3.28)

# Third-party projects that declare an older minimum version still get these behaviours:
# CMP0141 - MSVC debug information format is chosen with CMAKE_MSVC_DEBUG_INFORMATION_FORMAT (needed for ccache)
set(CMAKE_POLICY_DEFAULT_CMP0141 NEW)

project(Lodestone
	VERSION 0.1.0
	DESCRIPTION "A simple, production-grade 3D game engine"
	HOMEPAGE_URL "https://github.com/meirborowski/Lodestone"
	LANGUAGES C CXX
)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
	message(FATAL_ERROR "In-source builds aren't supported. Configure with a preset instead, e.g. `cmake --preset debug`.")
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

option(LS_BUILD_TESTS "Build the Lodestone tests" ON)
option(LS_ENABLE_SANITIZERS "Build everything with AddressSanitizer and UndefinedBehaviorSanitizer (GCC and Clang)" OFF)

include(BuildConfigurations)
include(CompilerSettings)
include(Dependencies)
include(LodestoneTarget)

add_subdirectory(Source/Core)
add_subdirectory(Source/Client)
add_subdirectory(Source/Runtime)
add_subdirectory(Source/Server)

# The editor never exists in Dist builds, so exported games can't contain editor code
# (see docs/Architecture.md#targets-and-layering)
if(LS_MULTI_CONFIG OR NOT CMAKE_BUILD_TYPE STREQUAL "Dist")
	add_subdirectory(Source/Editor)
endif()

if(LS_BUILD_TESTS)
	enable_testing()
	add_subdirectory(Tests)
endif()
