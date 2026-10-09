# lavapipe - Mesa's software Vulkan driver. Reference-image tests always render with it, at the pinned Mesa version in
# tools/lavapipe.env, so results don't depend on the GPU or driver (see docs/Testing.md#reference-images and
# docs/Decisions/0009-lavapipe.md).
#
# Sets LS_LAVAPIPE_DRIVER to lavapipe's driver library, which rendering tests load directly, or
# LS_LAVAPIPE_UNAVAILABLE_REASON when there's none.

option(LS_REQUIRE_LAVAPIPE "Fail to configure when lavapipe isn't available, instead of skipping reference-image tests (CI sets this)" OFF)
set(LS_LAVAPIPE_CACHE_DIR "${PROJECT_SOURCE_DIR}/.cache/lavapipe" CACHE PATH "Directory where lavapipe is installed")

file(STRINGS "${PROJECT_SOURCE_DIR}/tools/lavapipe.env" LS_LAVAPIPE_SETTINGS REGEX "^[A-Z_0-9]+=")
foreach(setting IN LISTS LS_LAVAPIPE_SETTINGS)
	string(REGEX MATCH "^([A-Z_0-9]+)=(.*)$" _ "${setting}")
	set(LS_LAVAPIPE_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
endforeach()

set(LS_LAVAPIPE_DRIVER "")
set(LS_LAVAPIPE_UNAVAILABLE_REASON "")

if(WIN32)
	# mesa-dist-win's release (MIT). Only lavapipe is extracted, into a directory every build tree shares
	set(installDirectory "${LS_LAVAPIPE_CACHE_DIR}/mesa-${LS_LAVAPIPE_MESA_VERSION}-windows")
	set(LS_LAVAPIPE_DRIVER "${installDirectory}/x64/vulkan_lvp.dll")
	if(NOT EXISTS "${LS_LAVAPIPE_DRIVER}")
		set(archive "${LS_DEPENDENCY_CACHE_DIR}/mesa-windows-${LS_LAVAPIPE_MESA_WINDOWS_ARCHIVE_SHA256}.7z")
		set(url "https://github.com/pal1000/mesa-dist-win/releases/download/${LS_LAVAPIPE_MESA_VERSION}/mesa3d-${LS_LAVAPIPE_MESA_VERSION}-release-msvc.7z")
		if(EXISTS "${archive}")
			file(SHA256 "${archive}" archiveHash)
		endif()
		if(NOT archiveHash STREQUAL LS_LAVAPIPE_MESA_WINDOWS_ARCHIVE_SHA256)
			message(STATUS "Downloading lavapipe (Mesa ${LS_LAVAPIPE_MESA_VERSION})")
			file(DOWNLOAD "${url}" "${archive}" EXPECTED_HASH SHA256=${LS_LAVAPIPE_MESA_WINDOWS_ARCHIVE_SHA256} STATUS status)
			list(GET status 0 statusCode)
			if(NOT statusCode EQUAL 0)
				file(REMOVE "${archive}")
				message(FATAL_ERROR "Can't download lavapipe from ${url}: ${status}")
			endif()
		endif()
		file(REMOVE_RECURSE "${installDirectory}.partial")
		file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${installDirectory}.partial" PATTERNS "x64/*lvp*")
		file(RENAME "${installDirectory}.partial" "${installDirectory}")
	endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	# Built from source by tools/build-lavapipe.sh, which CI runs and caches
	set(LS_LAVAPIPE_DRIVER "${LS_LAVAPIPE_CACHE_DIR}/mesa-${LS_LAVAPIPE_MESA_VERSION}-linux/lib/libvulkan_lvp.so")
	if(NOT EXISTS "${LS_LAVAPIPE_DRIVER}")
		set(LS_LAVAPIPE_UNAVAILABLE_REASON "lavapipe isn't built. Build it with: tools/build-lavapipe.sh")
		set(LS_LAVAPIPE_DRIVER "")
	endif()
else()
	set(LS_LAVAPIPE_UNAVAILABLE_REASON "lavapipe isn't available on macOS, so reference-image tests are skipped there (see docs/Decisions/0009-lavapipe.md)")
endif()

if(LS_LAVAPIPE_DRIVER)
	message(STATUS "lavapipe (Mesa ${LS_LAVAPIPE_MESA_VERSION}): ${LS_LAVAPIPE_DRIVER}")
elseif(LS_REQUIRE_LAVAPIPE)
	message(FATAL_ERROR "LS_REQUIRE_LAVAPIPE is on, but ${LS_LAVAPIPE_UNAVAILABLE_REASON}")
else()
	message(STATUS "Reference-image tests will be skipped: ${LS_LAVAPIPE_UNAVAILABLE_REASON}")
endif()
