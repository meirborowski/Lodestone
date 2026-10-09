# LodestoneCore: ECS, scenes, assets and the simulation. It never links GLFW, nvrhi or miniaudio, so the simulation
# runs without a window, GPU or audio device (see docs/Architecture.md#targets-and-layering)

add_library(LodestoneCore STATIC)
add_library(Lodestone::Core ALIAS LodestoneCore)

target_sources(LodestoneCore PRIVATE
	Lodestone/Core/Assert.cpp
	Lodestone/Core/Assert.h
	Lodestone/Core/Base.h
	Lodestone/Core/CommandLine.cpp
	Lodestone/Core/CommandLine.h
	Lodestone/Core/Debugger.cpp
	Lodestone/Core/Debugger.h
	Lodestone/Core/Environment.cpp
	Lodestone/Core/Environment.h
	Lodestone/Core/Error.cpp
	Lodestone/Core/Error.h
	Lodestone/Core/Image.cpp
	Lodestone/Core/Image.h
	Lodestone/Core/Log.cpp
	Lodestone/Core/Log.h
	Lodestone/Core/RunMain.cpp
	Lodestone/Core/RunMain.h
)

set(LS_CORE_GENERATED_DIR "${PROJECT_BINARY_DIR}/Generated/Core")
configure_file(Lodestone/Core/Version.h.in "${LS_CORE_GENERATED_DIR}/Lodestone/Core/Version.h" @ONLY)
target_sources(LodestoneCore PRIVATE "${LS_CORE_GENERATED_DIR}/Lodestone/Core/Version.h")

target_include_directories(LodestoneCore PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${LS_CORE_GENERATED_DIR}"
)

# Exactly one build configuration define, for everything that links LodestoneCore (see Lodestone/Core/Base.h)
target_compile_definitions(LodestoneCore PUBLIC
	$<$<CONFIG:Debug>:LS_CONFIG_DEBUG>
	$<$<CONFIG:Release>:LS_CONFIG_RELEASE>
	$<$<CONFIG:Dist>:LS_CONFIG_DIST>
)

target_link_libraries(LodestoneCore
	PUBLIC spdlog::spdlog glm::glm
	PRIVATE stb::stb
)

ls_configure_target(LodestoneCore)
