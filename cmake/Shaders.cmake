# Shader compilation: HLSL to SPIR-V with DXC, driven by NVIDIA's ShaderMake (see docs/Decisions/0008-shader-pipeline.md).
#
# Compiled shaders are embedded in the binaries as generated headers, so no shader files ship separately.

include(ExternalProject)

# DXC, the HLSL compiler. Windows and Linux use a pinned release; DXC has no macOS release, so macOS uses the DXC in
# the Vulkan SDK
if(CMAKE_HOST_WIN32)
	ls_declare_dependency(dxc POPULATE_ONLY
		URL https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/dxc_2026_09_29.zip
		SHA256 ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1
	)
	FetchContent_MakeAvailable(dxc)
	set(LS_DXC_EXECUTABLE "${dxc_SOURCE_DIR}/bin/x64/dxc.exe")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
	ls_declare_dependency(dxc POPULATE_ONLY
		URL https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/linux_dxc_2026_09_28.x86_x64.tar.gz
		SHA256 96faadc7f5c282d2ffda49804beb4c3ee38127bc252b723234e3c5cdf7aa39a1
	)
	FetchContent_MakeAvailable(dxc)
	set(LS_DXC_EXECUTABLE "${dxc_SOURCE_DIR}/bin/dxc")
else()
	# The SDK's directory when VULKAN_SDK is set, otherwise the PATH (the SDK's system-wide installation links DXC into
	# /usr/local/bin)
	find_program(LS_DXC_EXECUTABLE dxc HINTS "$ENV{VULKAN_SDK}/bin")
	if(NOT LS_DXC_EXECUTABLE)
		message(FATAL_ERROR "DXC wasn't found. Install the Vulkan SDK (see docs/TechStack.md#build-prerequisites).")
	endif()
endif()
message(STATUS "Shader compiler: ${LS_DXC_EXECUTABLE}")

# ShaderMake - batch shader compilation front end (MIT). No releases, so pinned to a commit. It's a build tool, so it's
# built as a separate project: in Release, with the host compiler, and without Lodestone's flags or sanitizers
ls_declare_dependency(shadermake POPULATE_ONLY
	URL https://github.com/NVIDIA-RTX/ShaderMake/archive/3f01623a76b092ac127d2debd79a672f55d77665.tar.gz
	SHA256 d002172bb8347d0fcd1c65654110b293ae1f1c2f77a1b50e31fd94353004cc42
)
FetchContent_MakeAvailable(shadermake)

set(LS_SHADERMAKE_BINARY_DIR "${PROJECT_BINARY_DIR}/_tools/ShaderMake")
set(LS_SHADERMAKE_EXECUTABLE "${LS_SHADERMAKE_BINARY_DIR}/ShaderMake${CMAKE_EXECUTABLE_SUFFIX}")
ExternalProject_Add(ShaderMakeTool
	SOURCE_DIR "${shadermake_SOURCE_DIR}/ShaderMake"
	BINARY_DIR "${LS_SHADERMAKE_BINARY_DIR}"
	CMAKE_ARGS
		-DCMAKE_BUILD_TYPE=Release
		-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
		-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
		-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${LS_SHADERMAKE_BINARY_DIR}
		-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE=${LS_SHADERMAKE_BINARY_DIR}
	BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --config Release --target ShaderMake
	BUILD_BYPRODUCTS "${LS_SHADERMAKE_EXECUTABLE}"
	INSTALL_COMMAND ""
	EXCLUDE_FROM_ALL TRUE
)

# ls_add_shaders(<target> CONFIG <config file> SOURCES <hlsl files>... OUTPUTS <generated headers>...)
#
# Compiles the shaders listed in a ShaderMake config file to SPIR-V before <target> is built, as headers in
# ${PROJECT_BINARY_DIR}/Generated/Shaders that <target> can include as "Shaders/<name>.spirv.h". OUTPUTS lists the
# headers ShaderMake generates: for "Triangle.hlsl -E VSMain" that's "Triangle_VSMain.spirv.h".
function(ls_add_shaders target)
	cmake_parse_arguments(PARSE_ARGV 1 ARG "" "CONFIG" "SOURCES;OUTPUTS")
	if(NOT ARG_CONFIG OR NOT ARG_SOURCES OR NOT ARG_OUTPUTS)
		message(FATAL_ERROR "ls_add_shaders(${target}) needs CONFIG, SOURCES and OUTPUTS")
	endif()

	set(outputDirectory "${PROJECT_BINARY_DIR}/Generated/Shaders")
	list(TRANSFORM ARG_OUTPUTS PREPEND "${outputDirectory}/Shaders/")
	cmake_path(ABSOLUTE_PATH ARG_CONFIG BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT_VARIABLE configPath)
	cmake_path(GET configPath PARENT_PATH sourceDirectory)

	add_custom_command(
		OUTPUT ${ARG_OUTPUTS}
		COMMAND "${LS_SHADERMAKE_EXECUTABLE}"
			--platform SPIRV
			--header
			--config "${configPath}"
			--sourceDir "${sourceDirectory}"
			--out "${outputDirectory}/Shaders"
			--compiler "${LS_DXC_EXECUTABLE}"
			--shaderModel 6_5
			--vulkanVersion 1.3
			--WX
			# Match NVRHI's default Vulkan binding offsets for each HLSL register type
			--tRegShift 0 --sRegShift 128 --bRegShift 256 --uRegShift 384
			--project Lodestone
		DEPENDS ShaderMakeTool "${LS_SHADERMAKE_EXECUTABLE}" "${LS_DXC_EXECUTABLE}" "${configPath}" ${ARG_SOURCES}
		COMMENT "Compiling shaders for ${target}"
		VERBATIM
	)
	add_custom_target(${target}Shaders DEPENDS ${ARG_OUTPUTS})
	add_dependencies(${target} ${target}Shaders)
	target_include_directories(${target} PRIVATE "${outputDirectory}")

	# Listed in the target for IDEs only: ShaderMake compiles them, not the C++ compiler or Visual Studio's FXC
	target_sources(${target} PRIVATE ${ARG_SOURCES} "${configPath}")
	set_source_files_properties(${ARG_SOURCES} "${configPath}" PROPERTIES HEADER_FILE_ONLY TRUE)
endfunction()
