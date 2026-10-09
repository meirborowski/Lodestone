#pragma once

#include "Lodestone/Core/Error.h"

#include <vulkan/vulkan_core.h>

#include <expected>

namespace Lodestone {

	// Loads the Vulkan loader library (vulkan-1.dll, libvulkan.so.1 or libvulkan.1.dylib) on first use, and returns
	// its vkGetInstanceProcAddr. The library stays loaded until the process ends. Windows and graphics devices share
	// it, so GLFW and the renderer always talk to the same loader. Thread-safe
	[[nodiscard]] std::expected<PFN_vkGetInstanceProcAddr, Error> LoadVulkanLibrary();

}
