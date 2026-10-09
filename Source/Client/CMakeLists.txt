# LodestoneClient: the window, device input, renderer and audio - everything a player needs that a dedicated server
# doesn't (see docs/Architecture.md#targets-and-layering)

add_library(LodestoneClient STATIC)
add_library(Lodestone::Client ALIAS LodestoneClient)

target_sources(LodestoneClient PRIVATE
	Lodestone/Graphics/DebugSceneRenderer.cpp
	Lodestone/Graphics/DebugSceneRenderer.h
	Lodestone/Graphics/GraphicsDevice.cpp
	Lodestone/Graphics/GraphicsDevice.h
	Lodestone/Graphics/ImGuiRenderer.cpp
	Lodestone/Graphics/ImGuiRenderer.h
	Lodestone/Graphics/RenderTarget.cpp
	Lodestone/Graphics/RenderTarget.h
	Lodestone/Graphics/Swapchain.cpp
	Lodestone/Graphics/Swapchain.h
	Lodestone/Graphics/TextureReadback.cpp
	Lodestone/Graphics/TextureReadback.h
	Lodestone/Graphics/TriangleRenderer.cpp
	Lodestone/Graphics/TriangleRenderer.h
	Lodestone/Graphics/Vulkan.h
	Lodestone/Graphics/VulkanLibrary.cpp
	Lodestone/Graphics/VulkanLibrary.h
	Lodestone/Input/DeviceInput.cpp
	Lodestone/Input/DeviceInput.h
	Lodestone/Input/InputCommandBuilder.cpp
	Lodestone/Input/InputCommandBuilder.h
	Lodestone/Platform/ImGuiPlatform.cpp
	Lodestone/Platform/ImGuiPlatform.h
	Lodestone/Platform/Window.cpp
	Lodestone/Platform/Window.h
	ThirdPartyConfig/imgui/LodestoneImGuiConfig.h
)

ls_add_shaders(LodestoneClient
	CONFIG Shaders/Shaders.cfg
	SOURCES
		Shaders/DebugCube.hlsl
		Shaders/DebugGrid.hlsl
		Shaders/ImGui.hlsl
		Shaders/Triangle.hlsl
	OUTPUTS
		DebugCube_VSMain.spirv.h DebugCube_PSMain.spirv.h
		DebugGrid_VSMain.spirv.h DebugGrid_PSMain.spirv.h
		ImGui_VSMain.spirv.h ImGui_PSMain.spirv.h
		Triangle_VSMain.spirv.h Triangle_PSMain.spirv.h
)

target_include_directories(LodestoneClient PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(LodestoneClient
	PUBLIC LodestoneCore nvrhi Vulkan::Headers imgui
	PRIVATE nvrhi_vk glfw
)

target_compile_definitions(LodestoneClient PUBLIC
	# Vulkan functions are loaded at runtime through vulkan.hpp's dispatcher, as NVRHI does (see Lodestone/Graphics/Vulkan.h)
	VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
	# GLFW must not include the OpenGL headers
	GLFW_INCLUDE_NONE
)

ls_configure_target(LodestoneClient)
