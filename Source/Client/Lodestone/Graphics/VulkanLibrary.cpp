#include "Lodestone/Graphics/VulkanLibrary.h"

#include "Lodestone/Graphics/Vulkan.h"

#include <nvrhi/vulkan.h>
#include <spdlog/fmt/fmt.h>

#include <exception>

// The storage of vulkan.hpp's default dispatcher, which must be defined in exactly one file of the program
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace Lodestone {

	namespace Vulkan {

		Error MakeError(std::string_view operation, VkResult result)
		{
			return Error(
				ErrorCode::DeviceError, fmt::format("{} failed: {}", operation, nvrhi::vulkan::resultToString(result)));
		}

	}

	std::expected<PFN_vkGetInstanceProcAddr, Error> LoadVulkanLibrary()
	{
		static const std::expected<PFN_vkGetInstanceProcAddr, Error> LoadResult =
			[]() -> std::expected<PFN_vkGetInstanceProcAddr, Error>
		{
			// vulkan.hpp's loader reports failure with an exception, which must not escape into engine code
			try
			{
				// Intentionally never destroyed: windows and devices may use Vulkan until the process ends
				static auto* s_Library = new vk::detail::DynamicLoader();
				const auto getInstanceProcAddr =
					s_Library->getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
				if (getInstanceProcAddr == nullptr)
					return std::unexpected(
						Error(ErrorCode::DeviceError, "The Vulkan library doesn't export vkGetInstanceProcAddr"));

				Vulkan::Dispatch().init(getInstanceProcAddr);
				return getInstanceProcAddr;
			}
			catch (const std::exception& exception)
			{
				return std::unexpected(Error(ErrorCode::DeviceError,
					fmt::format("Can't load the Vulkan library ({}). Install a graphics driver that supports Vulkan",
						exception.what())));
			}
		}();
		return LoadResult;
	}

}
