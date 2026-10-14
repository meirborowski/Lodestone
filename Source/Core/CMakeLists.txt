# LodestoneCore: ECS, scenes, assets and the simulation. It never links GLFW, nvrhi or miniaudio, so the simulation
# runs without a window, GPU or audio device (see docs/Architecture.md#targets-and-layering)

add_library(LodestoneCore STATIC)
add_library(Lodestone::Core ALIAS LodestoneCore)

target_sources(LodestoneCore PRIVATE
	Lodestone/Animation/Animation.cpp
	Lodestone/Animation/Animation.h
	Lodestone/Animation/AnimationComponents.cpp
	Lodestone/Animation/AnimationComponents.h
	Lodestone/Animation/ScenePose.cpp
	Lodestone/Animation/ScenePose.h
	Lodestone/Asset/AssetMetadata.cpp
	Lodestone/Asset/AssetMetadata.h
	Lodestone/Asset/AssetRegistry.cpp
	Lodestone/Asset/AssetRegistry.h
	Lodestone/Asset/Model.cpp
	Lodestone/Asset/Model.h
	Lodestone/Asset/ModelCache.cpp
	Lodestone/Asset/ModelCache.h
	Lodestone/Asset/ModelImporter.cpp
	Lodestone/Asset/ModelImporter.h
	Lodestone/Core/Assert.cpp
	Lodestone/Core/Assert.h
	Lodestone/Core/Base.h
	Lodestone/Core/Base64.cpp
	Lodestone/Core/Base64.h
	Lodestone/Core/BoundingBox.cpp
	Lodestone/Core/BoundingBox.h
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
	Lodestone/Core/HdrImage.cpp
	Lodestone/Core/HdrImage.h
	Lodestone/Core/Image.cpp
	Lodestone/Core/Image.h
	Lodestone/Core/Log.cpp
	Lodestone/Core/Log.h
	Lodestone/Core/RandomGenerator.cpp
	Lodestone/Core/RandomGenerator.h
	Lodestone/Core/Rotation.cpp
	Lodestone/Core/Rotation.h
	Lodestone/Core/RunMain.cpp
	Lodestone/Core/RunMain.h
	Lodestone/Core/UUID.cpp
	Lodestone/Core/UUID.h
	Lodestone/Input/InputCodes.cpp
	Lodestone/Input/InputCodes.h
	Lodestone/Input/InputCommand.h
	Lodestone/Physics/BodyIdAllocator.cpp
	Lodestone/Physics/BodyIdAllocator.h
	Lodestone/Physics/JoltRuntime.cpp
	Lodestone/Physics/JoltRuntime.h
	Lodestone/Physics/PhysicsComponents.cpp
	Lodestone/Physics/PhysicsComponents.h
	Lodestone/Physics/PhysicsLayers.cpp
	Lodestone/Physics/PhysicsLayers.h
	Lodestone/Physics/PhysicsWorld.cpp
	Lodestone/Physics/PhysicsWorld.h
	Lodestone/Physics/StateBuffer.cpp
	Lodestone/Physics/StateBuffer.h
	Lodestone/Project/Project.cpp
	Lodestone/Project/Project.h
	Lodestone/Reflection/ComponentRegistry.cpp
	Lodestone/Reflection/ComponentRegistry.h
	Lodestone/Reflection/FieldValue.cpp
	Lodestone/Reflection/FieldValue.h
	Lodestone/Reflection/SnapshotArchive.h
	Lodestone/Scripting/LuaAnnotations.cpp
	Lodestone/Scripting/LuaAnnotations.h
	Lodestone/Scripting/LuaApi.lua
	Lodestone/Scripting/ScriptAssets.cpp
	Lodestone/Scripting/ScriptAssets.h
	Lodestone/Scripting/ScriptBindings.cpp
	Lodestone/Scripting/ScriptBindings.h
	Lodestone/Scripting/ScriptComponents.cpp
	Lodestone/Scripting/ScriptComponents.h
	Lodestone/Scripting/ScriptRuntime.cpp
	Lodestone/Scripting/ScriptRuntime.h
	Lodestone/Scripting/ScriptSystem.cpp
	Lodestone/Scripting/ScriptSystem.h
	Lodestone/Scene/Components.cpp
	Lodestone/Scene/Components.h
	Lodestone/Scene/Entity.h
	Lodestone/Scene/EntitySerializer.cpp
	Lodestone/Scene/EntitySerializer.h
	Lodestone/Scene/ModelEntities.cpp
	Lodestone/Scene/ModelEntities.h
	Lodestone/Scene/PrefabSerializer.cpp
	Lodestone/Scene/PrefabSerializer.h
	Lodestone/Scene/RenderComponents.cpp
	Lodestone/Scene/RenderComponents.h
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

# sol2's templates give the files that bind the scripting API more sections than MSVC's default object format holds
if(MSVC)
	set_source_files_properties(
		Lodestone/Scripting/ScriptBindings.cpp Lodestone/Scripting/ScriptRuntime.cpp Lodestone/Scripting/ScriptSystem.cpp
		PROPERTIES COMPILE_OPTIONS /bigobj)
endif()

set(LS_CORE_GENERATED_DIR "${PROJECT_BINARY_DIR}/Generated/Core")
configure_file(Lodestone/Core/Version.h.in "${LS_CORE_GENERATED_DIR}/Lodestone/Core/Version.h" @ONLY)
target_sources(LodestoneCore PRIVATE "${LS_CORE_GENERATED_DIR}/Lodestone/Core/Version.h")

# The scripting API's hand-written annotations, embedded as character literals for LuaAnnotations.cpp. Configuring again
# when the file changes keeps the copy current
set(LS_LUA_API "${CMAKE_CURRENT_SOURCE_DIR}/Lodestone/Scripting/LuaApi.lua")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${LS_LUA_API}")
file(READ "${LS_LUA_API}" LS_LUA_API_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "'\\\\x\\1'," LS_LUA_API_CHARACTERS "${LS_LUA_API_HEX}")
file(CONFIGURE OUTPUT "${LS_CORE_GENERATED_DIR}/Lodestone/Scripting/LuaApi.inc" CONTENT "${LS_LUA_API_CHARACTERS}\n")

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
	PRIVATE stb::stb cgltf::cgltf Jolt::Jolt Lodestone::Lua
)

ls_configure_target(LodestoneCore)
