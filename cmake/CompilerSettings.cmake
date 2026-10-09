# Language standard and toolchain-wide settings. These apply to every target, third-party ones included.
# Settings that apply only to Lodestone's own code live in LodestoneTarget.cmake.

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Lodestone doesn't use C++20 modules, so skip the dependency scan CMake would otherwise run for C++20 and newer
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Minimum compiler versions (see docs/TechStack.md#platforms--compilers)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	set(LS_MINIMUM_COMPILER_VERSION 14)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
	# Clang 18 can't use std::expected with libstdc++
	set(LS_MINIMUM_COMPILER_VERSION 19)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
	# Apple Clang 17 ships with Xcode 16.3
	set(LS_MINIMUM_COMPILER_VERSION 17)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
	# MSVC Build Tools 14.50 (compiler 19.50) ship with Visual Studio 2026
	set(LS_MINIMUM_COMPILER_VERSION 19.50)
else()
	message(FATAL_ERROR "Unsupported C++ compiler '${CMAKE_CXX_COMPILER_ID}'. Use MSVC, GCC, Clang or Apple Clang.")
endif()
if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS LS_MINIMUM_COMPILER_VERSION)
	message(FATAL_ERROR "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} is too old. Lodestone needs ${LS_MINIMUM_COMPILER_VERSION} or newer.")
endif()
message(STATUS "Lodestone compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

if(MSVC)
	# Source files are UTF-8 (fmt requires this), and __cplusplus reports the real language version
	add_compile_options(/utf-8 $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>)
	# Exclude rarely used Windows headers, and keep windows.h from defining min and max macros
	add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

if(LS_ENABLE_SANITIZERS)
	if(MSVC)
		message(FATAL_ERROR "LS_ENABLE_SANITIZERS supports GCC and Clang only")
	endif()
	# Applied globally, so third-party code is instrumented too and containers can be checked across library boundaries
	set(LS_SANITIZER_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
	add_compile_options(${LS_SANITIZER_FLAGS})
	add_link_options(${LS_SANITIZER_FLAGS})
	message(STATUS "Building with AddressSanitizer and UndefinedBehaviorSanitizer")
endif()

if(LS_ENABLE_THREAD_SANITIZER)
	if(MSVC)
		message(FATAL_ERROR "LS_ENABLE_THREAD_SANITIZER supports GCC and Clang only")
	endif()
	if(LS_ENABLE_SANITIZERS)
		message(FATAL_ERROR "ThreadSanitizer can't be combined with AddressSanitizer: use one preset or the other")
	endif()
	set(LS_SANITIZER_FLAGS -fsanitize=thread -fno-omit-frame-pointer)
	add_compile_options(${LS_SANITIZER_FLAGS})
	add_link_options(${LS_SANITIZER_FLAGS})
	message(STATUS "Building with ThreadSanitizer")
endif()
