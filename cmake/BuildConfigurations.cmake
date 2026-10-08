# Lodestone's build configurations (see docs/TechStack.md#build-configurations):
#   Debug   - no optimization, asserts and developer logging
#   Release - optimized, with debug information, asserts, developer logging and the editor
#   Dist    - fully optimized, without the editor, asserts or developer logging. Used for exported games
#
# The configuration is exposed to code as exactly one of LS_CONFIG_DEBUG, LS_CONFIG_RELEASE or LS_CONFIG_DIST,
# defined by LodestoneCore for everything that links it.

set(LS_CONFIGURATIONS Debug Release Dist)

get_property(LS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(LS_MULTI_CONFIG)
	set(CMAKE_CONFIGURATION_TYPES ${LS_CONFIGURATIONS} CACHE STRING "Build configurations" FORCE)
else()
	if(NOT CMAKE_BUILD_TYPE)
		set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build configuration" FORCE)
	endif()
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS ${LS_CONFIGURATIONS})
	if(NOT CMAKE_BUILD_TYPE IN_LIST LS_CONFIGURATIONS)
		message(FATAL_ERROR "Unsupported CMAKE_BUILD_TYPE '${CMAKE_BUILD_TYPE}'. Use one of: ${LS_CONFIGURATIONS}")
	endif()
	message(STATUS "Lodestone build configuration: ${CMAKE_BUILD_TYPE}")
endif()

# Dist starts from CMake's Release flags (full optimization, NDEBUG). CMake creates empty cache entries for the flags
# of a custom configuration when it enables a language, so they're overwritten once, on the first configure, and
# left alone afterwards so they can still be customized
if(NOT LS_DIST_FLAGS_INITIALIZED)
	foreach(lang IN ITEMS C CXX)
		set(CMAKE_${lang}_FLAGS_DIST "${CMAKE_${lang}_FLAGS_RELEASE}" CACHE STRING "Flags used by the ${lang} compiler in Dist builds" FORCE)
		mark_as_advanced(CMAKE_${lang}_FLAGS_DIST)
	endforeach()
	foreach(kind IN ITEMS EXE SHARED MODULE STATIC)
		set(CMAKE_${kind}_LINKER_FLAGS_DIST "${CMAKE_${kind}_LINKER_FLAGS_RELEASE}" CACHE STRING "Linker flags for ${kind} targets in Dist builds" FORCE)
		mark_as_advanced(CMAKE_${kind}_LINKER_FLAGS_DIST)
	endforeach()
	set(LS_DIST_FLAGS_INITIALIZED ON CACHE INTERNAL "Whether the Dist flags have been initialized from the Release flags")
endif()

# Release keeps debug information, so optimized builds can be profiled and debugged
if(MSVC)
	# Embedded (/Z7) debug information keeps every object file self-contained, which compiler caches require
	set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,Release>:Embedded>")
	add_link_options($<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release,Dist>:/OPT:REF> $<$<CONFIG:Release,Dist>:/OPT:ICF>)
else()
	add_compile_options($<$<CONFIG:Release>:-g>)
endif()

# Dist uses link-time optimization where the toolchain supports it
include(CheckIPOSupported)
check_ipo_supported(RESULT LS_IPO_SUPPORTED OUTPUT LS_IPO_OUTPUT LANGUAGES C CXX)
if(LS_IPO_SUPPORTED)
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DIST ON)
else()
	message(STATUS "Link-time optimization isn't supported by this toolchain, so Dist builds without it: ${LS_IPO_OUTPUT}")
endif()
