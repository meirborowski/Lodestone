# LodestoneClient: the window, device input, renderer and audio - everything a player needs that a dedicated server
# doesn't (see docs/Architecture.md#targets-and-layering)

add_library(LodestoneClient STATIC)
add_library(Lodestone::Client ALIAS LodestoneClient)

target_sources(LodestoneClient PRIVATE
	Lodestone/Graphics/GraphicsDevice.cpp
	Lodestone/Graphics/GraphicsDevice.h
	Lodestone/Graphics/ImGuiRenderer.cpp
	Lodestone/Graphics/ImGuiRenderer.h
	Lodestone/Graphics/RenderTarget.cpp
	Lodestone/Graphics/RenderTarget.h
	Lodestone/Graphics/ShaderModule.h
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
	Lodestone/Renderer/EnvironmentMap.cpp
	Lodestone/Renderer/EnvironmentMap.h
	Lodestone/Renderer/Frustum.cpp
	Lodestone/Renderer/Frustum.h
	Lodestone/Renderer/GpuModel.cpp
	Lodestone/Renderer/GpuModel.h
	Lodestone/Renderer/ImageBasedLighting.cpp
	Lodestone/Renderer/ImageBasedLighting.h
	Lodestone/Renderer/LineRenderer.cpp
	Lodestone/Renderer/LineRenderer.h
	Lodestone/Renderer/MipChain.cpp
	Lodestone/Renderer/MipChain.h
	Lodestone/Renderer/RenderAssets.cpp
	Lodestone/Renderer/RenderAssets.h
	Lodestone/Renderer/RenderResources.cpp
	Lodestone/Renderer/RenderResources.h
	Lodestone/Renderer/RenderStats.cpp
	Lodestone/Renderer/RenderStats.h
	Lodestone/Renderer/RenderWorld.cpp
	Lodestone/Renderer/RenderWorld.h
	Lodestone/Renderer/SceneRenderer.cpp
	Lodestone/Renderer/SceneRenderer.h
	Lodestone/Renderer/SceneRenderTarget.cpp
	Lodestone/Renderer/SceneRenderTarget.h
	Lodestone/Renderer/ShaderTypes.h
	Lodestone/Renderer/ShadowCascades.cpp
	Lodestone/Renderer/ShadowCascades.h
	ThirdPartyConfig/imgui/LodestoneImGuiConfig.h
)

ls_add_shaders(LodestoneClient
	CONFIG Shaders/Shaders.cfg
	SOURCES
		Shaders/AmbientOcclusion.hlsl
		Shaders/Common.hlsli
		Shaders/Environment.hlsl
		Shaders/EnvironmentMapping.hlsli
		Shaders/Fullscreen.hlsl
		Shaders/Fullscreen.hlsli
		Shaders/ImGui.hlsl
		Shaders/Lighting.hlsli
		Shaders/Lines.hlsl
		Shaders/Mesh.hlsl
		Shaders/Sky.hlsl
		Shaders/Tonemap.hlsl
		Shaders/Triangle.hlsl
	OUTPUTS
		AmbientOcclusion_PSOcclusion.spirv.h AmbientOcclusion_PSBlur.spirv.h
		Environment_CSEquirectangularToCube.spirv.h Environment_CSDownsampleCube.spirv.h
		Environment_CSPrefilter.spirv.h
		Fullscreen_VSMain.spirv.h
		ImGui_VSMain.spirv.h ImGui_PSMain.spirv.h
		Lines_VSMain.spirv.h Lines_PSMain.spirv.h
		Mesh_VSMain.spirv.h Mesh_VSShadow.spirv.h Mesh_VSMainSkinned.spirv.h Mesh_VSShadowSkinned.spirv.h
		Mesh_PSShadowMasked.spirv.h
		Mesh_PSPrepass.spirv.h Mesh_PSPrepassMasked.spirv.h
		Mesh_PSForward.spirv.h Mesh_PSForwardMasked.spirv.h Mesh_PSForwardBlended.spirv.h
		Sky_PSMain.spirv.h
		Tonemap_PSMain.spirv.h
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
