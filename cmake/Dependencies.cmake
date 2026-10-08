# Third-party dependencies, fetched with FetchContent and pinned to exact versions (see docs/TechStack.md#libraries).
#
# Every dependency is downloaded as an archive and verified against its SHA256 hash. Archives are kept in
# LS_DEPENDENCY_CACHE_DIR, which build trees share and CI caches, so each one is downloaded only once.
# When adding a dependency, also add it to THIRD_PARTY_LICENSES.md.

include(FetchContent)

set(LS_DEPENDENCY_CACHE_DIR "${PROJECT_SOURCE_DIR}/.cache/dependencies" CACHE PATH "Directory where downloaded dependency archives are kept")

# Don't contact the network to update content that's already been populated
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

# ls_declare_dependency(<name> URL <url> SHA256 <hash>)
#
# Declares a dependency downloaded from an archive. The dependency's include directories are SYSTEM, so its
# headers don't trigger Lodestone's warnings, and its targets are excluded from the default build target unless
# something Lodestone builds needs them.
function(ls_declare_dependency name)
	cmake_parse_arguments(PARSE_ARGV 1 ARG "" "URL;SHA256" "")
	if(NOT ARG_URL OR NOT ARG_SHA256)
		message(FATAL_ERROR "ls_declare_dependency(${name}) needs both URL and SHA256")
	endif()
	FetchContent_Declare(${name}
		URL "${ARG_URL}"
		URL_HASH SHA256=${ARG_SHA256}
		DOWNLOAD_DIR "${LS_DEPENDENCY_CACHE_DIR}"
		# The hash is part of the file name, so switching between versions never re-downloads an archive
		DOWNLOAD_NAME "${name}-${ARG_SHA256}.tar.gz"
		DOWNLOAD_EXTRACT_TIMESTAMP OFF
		SYSTEM
		EXCLUDE_FROM_ALL
	)
endfunction()

# spdlog - logging (MIT). Uses its bundled copy of fmt (MIT)
ls_declare_dependency(spdlog
	URL https://github.com/gabime/spdlog/archive/refs/tags/v1.17.0.tar.gz
	SHA256 d8862955c6d74e5846b3f580b1605d2428b11d97a410d86e2fb13e857cd3a744
)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "" FORCE)
set(SPDLOG_USE_STD_FORMAT OFF CACHE BOOL "" FORCE)

# doctest - unit testing (MIT)
ls_declare_dependency(doctest
	URL https://github.com/doctest/doctest/archive/refs/tags/v2.5.3.tar.gz
	SHA256 174ebc4e769928959614789c5b4e9c3d0a0f81a62bb608756b127bfebfb21331
)
set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(DOCTEST_WITH_MAIN_IN_STATIC_LIB OFF CACHE BOOL "" FORCE)
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)

# Lua 5.4 - scripting (MIT). Lua has no CMake build of its own; the target is defined below
ls_declare_dependency(lua
	URL https://www.lua.org/ftp/lua-5.4.9.tar.gz
	SHA256 2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6
)

# sol2 - Lua bindings (MIT). sol2's releases are infrequent, so this is pinned to a develop-branch commit that
# builds cleanly with every supported compiler (it includes fixes for Clang 19 made after v3.5.0)
ls_declare_dependency(sol2
	URL https://github.com/ThePhD/sol2/archive/c1f95a773c6f8f4fde8ca3efe872e7286afe4444.tar.gz
	SHA256 f3b7bff03c260c74c74bd55f812f7e174d5c357f576430ffaf12a206462d6356
)
set(SOL2_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(SOL2_SYSTEM_INCLUDE ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(spdlog doctest lua sol2)

include("${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")

# Lua is compiled as C++, so Lua errors unwind C++ stack frames with exceptions (running destructors) instead of
# longjmp. See docs/Decisions/0003-lua-compiled-as-cpp.md
set(LS_LUA_SOURCES
	# Core
	lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c llex.c lmem.c lobject.c lopcodes.c lparser.c
	lstate.c lstring.c ltable.c ltm.c lundump.c lvm.c lzio.c
	# Standard libraries (the scripting sandbox decides which ones scripts can use)
	lauxlib.c lbaselib.c lcorolib.c ldblib.c liolib.c lmathlib.c loadlib.c loslib.c lstrlib.c ltablib.c lutf8lib.c
	linit.c
)
list(TRANSFORM LS_LUA_SOURCES PREPEND "${lua_SOURCE_DIR}/src/")
add_library(lua STATIC EXCLUDE_FROM_ALL ${LS_LUA_SOURCES})
add_library(Lua::Lua ALIAS lua)
set_source_files_properties(${LS_LUA_SOURCES} PROPERTIES LANGUAGE CXX)
target_include_directories(lua SYSTEM PUBLIC "${lua_SOURCE_DIR}/src")
target_compile_definitions(lua PRIVATE
	# POSIX functions such as mkstemp, without dlopen: native Lua modules are never loaded
	$<$<NOT:$<PLATFORM_ID:Windows>>:LUA_USE_POSIX>
	# Check arguments passed to the Lua C API in Debug builds
	$<$<CONFIG:Debug>:LUA_USE_APICHECK>
)

# Lua and sol2, configured the way Lodestone uses them
add_library(LodestoneLua INTERFACE)
add_library(Lodestone::Lua ALIAS LodestoneLua)
target_link_libraries(LodestoneLua INTERFACE Lua::Lua sol2::sol2)
target_compile_definitions(LodestoneLua INTERFACE
	# Lua is compiled as C++ (see above)
	SOL_USING_CXX_LUA=1
	# Check argument types and stack state in every configuration: scripts may be untrusted, and a bad call
	# must become a script error, never a crash
	SOL_ALL_SAFETIES_ON=1
)
