#pragma once

#include "Lodestone/Core/Error.h"

#include <vulkan/vulkan_core.h>

#include <expected>
#include <filesystem>

namespace Lodestone {

	// Loads the Vulkan loader library (vulkan-1.dll, libvulkan.so.1 or libvulkan.1.dylib) on first use, and returns
	// its vkGetInstanceProcAddr. The library stays loaded until the process ends. Windows and graphics devices share
	// it, so GLFW and the renderer always talk to the same loader. Thread-safe
	[[nodiscard]] std::expected<PFN_vkGetInstanceProcAddr, Error> LoadVulkanLibrary();

	// Loads a Vulkan driver library (an ICD, such as lavapipe's vulkan_lvp.dll or libvulkan_lvp.so), so the loader can
	// use it directly instead of the installed drivers, and returns its vk_icdGetInstanceProcAddr. Each library is
	// loaded once and stays loaded until the process ends. Thread-safe
	[[nodiscard]] std::expected<PFN_vkGetInstanceProcAddrLUNARG, Error> LoadVulkanDriver(
		const std::filesystem::path& library);

}
