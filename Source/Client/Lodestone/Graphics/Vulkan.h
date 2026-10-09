#pragma once

// Vulkan access for the graphics backend's implementation files. Lodestone doesn't link the Vulkan loader: every
// Vulkan call goes through vulkan.hpp's dynamic dispatcher, which LoadVulkanLibrary() fills from the loader it opens
// at runtime, and which NVRHI shares. Include this only in .cpp files - vulkan.hpp is large.

#include "Lodestone/Core/Error.h"

#include <vulkan/vulkan.hpp>

#include <string_view>

namespace Lodestone::Vulkan {

	// The process-wide table of Vulkan functions
	inline vk::detail::DispatchLoaderDynamic& Dispatch()
	{
		return VULKAN_HPP_DEFAULT_DISPATCHER;
	}

	// An error for a failed Vulkan call: "<operation> failed: VK_ERROR_DEVICE_LOST"
	Error MakeError(std::string_view operation, VkResult result);

}
