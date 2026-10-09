# LodestoneCore: ECS, scenes, assets and the simulation. It never links GLFW, nvrhi or miniaudio, so the simulation
# runs without a window, GPU or audio device (see docs/Architecture.md#targets-and-layering)

add_library(LodestoneCore STATIC)
add_library(Lodestone::Core ALIAS LodestoneCore)

target_sources(LodestoneCore PRIVATE
	Lodestone/Asset/AssetMetadata.cpp
	Lodestone/Asset/AssetMetadata.h
	Lodestone/Asset/AssetRegistry.cpp
	Lodestone/Asset/AssetRegistry.h
	Lodestone/Core/Assert.cpp
	Lodestone/Core/Assert.h
	Lodestone/Core/Base.h
	Lodestone/Core/Base64.cpp
	Lodestone/Core/Base64.h
	Lodestone/Core/CommandLine.cpp
	Lodestone/Core/CommandLine.h
	Lodestone/Core/Debugger.cpp
	Lodestone/Core/Debugger.h
	Lodestone/Core/EnumFlags.h
	Lodestone/Core/Environment.cpp
	Lodestone/Core/Environment.h
	Lodestone/Core/Error.cpp
	Lodestone/Core/Error.h
	Lodestone/Core/FileSystem.cpp
	Lodestone/Core/FileSystem.h
	Lodestone/Core/Image.cpp
	Lodestone/Core/Image.h
	Lodestone/Core/Log.cpp
	Lodestone/Core/Log.h
	Lodestone/Core/RandomGenerator.cpp
	Lodestone/Core/RandomGenerator.h
	Lodestone/Core/RunMain.cpp
	Lodestone/Core/RunMain.h
	Lodestone/Core/UUID.cpp
	Lodestone/Core/UUID.h
	Lodestone/Input/InputCodes.cpp
	Lodestone/Input/InputCodes.h
	Lodestone/Input/InputCommand.h
	Lodestone/Project/Project.cpp
	Lodestone/Project/Project.h
	Lodestone/Reflection/ComponentRegistry.cpp
	Lodestone/Reflection/ComponentRegistry.h
	Lodestone/Reflection/FieldValue.cpp
	Lodestone/Reflection/FieldValue.h
	Lodestone/Reflection/SnapshotArchive.h
	Lodestone/Scene/Components.cpp
	Lodestone/Scene/Components.h
	Lodestone/Scene/Entity.h
	Lodestone/Scene/EntitySerializer.cpp
	Lodestone/Scene/EntitySerializer.h
	Lodestone/Scene/PrefabSerializer.cpp
	Lodestone/Scene/PrefabSerializer.h
	Lodestone/Scene/Scene.cpp
	Lodestone/Scene/Scene.h
	Lodestone/Scene/SceneSerializer.cpp
	Lodestone/Scene/SceneSerializer.h
	Lodestone/Serialization/FileFormat.cpp
	Lodestone/Serialization/FileFormat.h
	Lodestone/Serialization/Json.cpp
	Lodestone/Serialization/Json.h
	Lodestone/Simulation/Simulation.cpp
	Lodestone/Simulation/Simulation.h
	ThirdPartyConfig/entt/ext/config.h
)

set(LS_CORE_GENERATED_DIR "${PROJECT_BINARY_DIR}/Generated/Core")
configure_file(Lodestone/Core/Version.h.in "${LS_CORE_GENERATED_DIR}/Lodestone/Core/Version.h" @ONLY)
target_sources(LodestoneCore PRIVATE "${LS_CORE_GENERATED_DIR}/Lodestone/Core/Version.h")

target_include_directories(LodestoneCore PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${LS_CORE_GENERATED_DIR}"
	# Configuration headers that third-party libraries include when they exist, such as <entt/ext/config.h>
	"${CMAKE_CURRENT_SOURCE_DIR}/ThirdPartyConfig"
)

# Exactly one build configuration define, for everything that links LodestoneCore (see Lodestone/Core/Base.h)
target_compile_definitions(LodestoneCore PUBLIC
	$<$<CONFIG:Debug>:LS_CONFIG_DEBUG>
	$<$<CONFIG:Release>:LS_CONFIG_RELEASE>
	$<$<CONFIG:Dist>:LS_CONFIG_DIST>
)

target_link_libraries(LodestoneCore
	PUBLIC spdlog::spdlog glm::glm EnTT::EnTT nlohmann_json::nlohmann_json
	PRIVATE stb::stb
)

ls_configure_target(LodestoneCore)
