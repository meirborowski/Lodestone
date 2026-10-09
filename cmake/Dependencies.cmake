# Third-party dependencies, fetched with FetchContent and pinned to exact versions (see docs/TechStack.md#libraries).
#
# Every dependency is downloaded as an archive and verified against its SHA256 hash. Archives are kept in
# LS_DEPENDENCY_CACHE_DIR, which build trees share and CI caches, so each one is downloaded only once.
# When adding a dependency, also add it to THIRD_PARTY_LICENSES.md.

include(FetchContent)

set(LS_DEPENDENCY_CACHE_DIR "${PROJECT_SOURCE_DIR}/.cache/dependencies" CACHE PATH "Directory where downloaded dependency archives are kept")

# Don't contact the network to update content that's already been populated
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

# ls_declare_dependency(<name> URL <url> SHA256 <hash> [POPULATE_ONLY])
#
# Declares a dependency downloaded from an archive (.tar.gz, .tar.xz, .zip or .7z). The dependency's include
# directories are SYSTEM, so its headers don't trigger Lodestone's warnings, and its targets are excluded from the
# default build target unless something Lodestone builds needs them. With POPULATE_ONLY, the archive is only
# extracted - its own CMake project, if any, isn't added to the build.
function(ls_declare_dependency name)
	cmake_parse_arguments(PARSE_ARGV 1 ARG "POPULATE_ONLY" "URL;SHA256" "")
	if(NOT ARG_URL OR NOT ARG_SHA256)
		message(FATAL_ERROR "ls_declare_dependency(${name}) needs both URL and SHA256")
	endif()
	if(NOT ARG_URL MATCHES "\\.(tar\\.gz|tar\\.xz|zip|7z)$")
		message(FATAL_ERROR "ls_declare_dependency(${name}): unsupported archive type in ${ARG_URL}")
	endif()
	set(extension "${CMAKE_MATCH_1}")

	set(populateOnly "")
	if(ARG_POPULATE_ONLY)
		# FetchContent_MakeAvailable() only adds a subdirectory that has a CMakeLists.txt
		set(populateOnly SOURCE_SUBDIR "lodestone-populate-only")
	endif()

	FetchContent_Declare(${name}
		URL "${ARG_URL}"
		URL_HASH SHA256=${ARG_SHA256}
		DOWNLOAD_DIR "${LS_DEPENDENCY_CACHE_DIR}"
		# The hash is part of the file name, so switching between versions never re-downloads an archive
		DOWNLOAD_NAME "${name}-${ARG_SHA256}.${extension}"
		DOWNLOAD_EXTRACT_TIMESTAMP OFF
		${populateOnly}
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

# glm - math (MIT)
ls_declare_dependency(glm
	URL https://github.com/g-truc/glm/archive/refs/tags/1.0.3.tar.gz
	SHA256 6775e47231a446fd086d660ecc18bcd076531cfedd912fbd66e576b118607001
)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)

# stb - image loading and writing (MIT or public domain). No releases, so pinned to a commit. stb has no CMake build;
# the target is defined below
ls_declare_dependency(stb
	URL https://github.com/nothings/stb/archive/2c980bb59875b0d32144a71867fbdebb2f77cd20.tar.gz
	SHA256 9a955b1b49a4410088a2e0ee2a9c057c3c907d0c1d75454144cb980aca0ba515
)

# GLFW - windows and device input (zlib)
ls_declare_dependency(glfw
	URL https://github.com/glfw/glfw/releases/download/3.5.1/glfw-3.5.1.zip
	SHA256 ea79bc5feffc254c87291980c2d0bce9acebb68c4983b79f961dcd2cb8a611a0
)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

# Vulkan-Headers - the Vulkan API headers, including vulkan.hpp (Apache 2.0 or MIT). Matches the pinned Vulkan SDK
ls_declare_dependency(vulkan_headers
	URL https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/vulkan-sdk-1.4.363.0.tar.gz
	SHA256 4a078be12bef21cfebc09d878b77a63cff9d68f899254a0b00d0e37ef73e7f7e
)
set(VULKAN_HEADERS_ENABLE_MODULE OFF CACHE BOOL "" FORCE)
set(VULKAN_HEADERS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(VULKAN_HEADERS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

# NVRHI - rendering hardware interface over Vulkan (MIT). No releases, so pinned to a commit. Lodestone renders with
# Vulkan on every platform, so the Direct3D backends are off
ls_declare_dependency(nvrhi
	URL https://github.com/NVIDIA-RTX/NVRHI/archive/6b96fb03e07539f08327aea76c56d55f1de9d906.tar.gz
	SHA256 f46c733ccc555fc457aaa3df510ee7df0ca089f5d1129d92903029e4fb67c560
)
set(NVRHI_WITH_VULKAN ON CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX11 OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX12 OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_VALIDATION ON CACHE BOOL "" FORCE)
set(NVRHI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(NVRHI_INSTALL OFF CACHE BOOL "" FORCE)
# Use the Vulkan-Headers declared above rather than fetching another copy
set(NVRHI_FETCH_VULKAN_HEADERS OFF CACHE BOOL "" FORCE)

# EnTT - the entity component system (MIT)
ls_declare_dependency(entt
	URL https://github.com/skypjack/entt/archive/refs/tags/v4.0.0.tar.gz
	SHA256 32a2ff2c72cb047dfd57306006ef238820b70da7c6ce4e7e8a507ac63365212e
)

# nlohmann/json - scene, prefab, project and asset metadata files (MIT). Implicit conversions are off, so reading a
# value always states the type it expects
ls_declare_dependency(nlohmann_json
	URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
	SHA256 42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
)
set(JSON_ImplicitConversions OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)

# Dear ImGui (docking branch) - the editor UI (MIT). It has no CMake build of its own; the target is defined below
ls_declare_dependency(imgui POPULATE_ONLY
	URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b-docking.tar.gz
	SHA256 90ded916bd57db2e0e171b6b098940a47c6f5042725dcdc67fb19940ca8bfdcc
)

# ImGuizmo - the editor's transform gizmo (MIT). No recent release, so pinned to a commit
ls_declare_dependency(imguizmo POPULATE_ONLY
	URL https://github.com/CedricGuillemet/ImGuizmo/archive/18cef5e031d8c6973d80284c67f60549fafd78c1.tar.gz
	SHA256 6ad626f0687be12c2f3ba6542c0f1bdda9e71e395d0645e4cda37695354406d8
)

# cpp-httplib - the HTTP transport of the editor's MCP server (MIT). Header-only; only the header is used
ls_declare_dependency(httplib POPULATE_ONLY
	URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.59.0.tar.gz
	SHA256 7c8cc7df044abb837d75f7c1e56333305acb40335c3788b8f5fbf5927e63e148
)

FetchContent_MakeAvailable(
	spdlog doctest lua sol2 glm stb glfw vulkan_headers nvrhi entt nlohmann_json imgui imguizmo httplib)

# NVRHI's Vulkan backend calls into NVRHI's common library without declaring it. Linkers that resolve symbols in one
# pass (GNU ld) need the common library after the backend on the command line, which this dependency guarantees
target_link_libraries(nvrhi_vk PUBLIC nvrhi)

# stb's single-file libraries, compiled once in a translation unit of their own, so third-party code is never built
# with Lodestone's warnings or checked by clang-tidy. Only the decoders Lodestone uses are compiled, and stb never
# touches files itself: Lodestone reads and writes them, so paths behave the same on every platform
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/stb/StbImplementation.cpp" CONTENT [[
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
]])
add_library(stb STATIC EXCLUDE_FROM_ALL "${CMAKE_CURRENT_BINARY_DIR}/stb/StbImplementation.cpp")
add_library(stb::stb ALIAS stb)
target_include_directories(stb SYSTEM PUBLIC "${stb_SOURCE_DIR}")
target_compile_definitions(stb PUBLIC STBI_ONLY_PNG STBI_ONLY_JPEG STBI_NO_STDIO STBI_WRITE_NO_STDIO)

include("${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")

# Dear ImGui with its GLFW platform backend. Lodestone renders it through NVRHI itself
# (Lodestone/Graphics/ImGuiRenderer), so no renderer backend is compiled
add_library(imgui STATIC EXCLUDE_FROM_ALL
	"${imgui_SOURCE_DIR}/imgui.cpp"
	"${imgui_SOURCE_DIR}/imgui_draw.cpp"
	"${imgui_SOURCE_DIR}/imgui_tables.cpp"
	"${imgui_SOURCE_DIR}/imgui_widgets.cpp"
	"${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
	"${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp"
)
add_library(imgui::imgui ALIAS imgui)
target_include_directories(imgui SYSTEM PUBLIC
	"${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends" "${imgui_SOURCE_DIR}/misc/cpp")
# Dear ImGui's asserts go through the engine's assert handler (see the configuration header), so it uses LodestoneCore
target_include_directories(imgui PUBLIC "${PROJECT_SOURCE_DIR}/Source/Client/ThirdPartyConfig/imgui")
target_compile_definitions(imgui PUBLIC
	IMGUI_DISABLE_OBSOLETE_FUNCTIONS
	IMGUI_USER_CONFIG="LodestoneImGuiConfig.h"
)
target_link_libraries(imgui PUBLIC LodestoneCore PRIVATE glfw)
# The GLFW backend mustn't include the OpenGL headers, as Lodestone renders with Vulkan
target_compile_definitions(imgui PRIVATE GLFW_INCLUDE_NONE)

add_library(imguizmo STATIC EXCLUDE_FROM_ALL "${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp")
add_library(imguizmo::imguizmo ALIAS imguizmo)
target_include_directories(imguizmo SYSTEM PUBLIC "${imguizmo_SOURCE_DIR}/src")
target_link_libraries(imguizmo PUBLIC imgui)

find_package(Threads REQUIRED)
add_library(httplib INTERFACE)
add_library(httplib::httplib ALIAS httplib)
target_include_directories(httplib SYSTEM INTERFACE "${httplib_SOURCE_DIR}")
target_link_libraries(httplib INTERFACE Threads::Threads $<$<PLATFORM_ID:Windows>:ws2_32>)
# Plain HTTP on the loopback interface only: no TLS, compression or other optional features
target_compile_definitions(httplib INTERFACE $<$<PLATFORM_ID:Windows>:_WIN32_WINNT=0x0A00>)

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
