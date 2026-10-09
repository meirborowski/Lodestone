#include "Lodestone/Graphics/VulkanLibrary.h"

#include "Lodestone/Core/Base.h"
#include "Lodestone/Graphics/Vulkan.h"

#include <nvrhi/vulkan.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/std.h>

#include <exception>
#include <map>
#include <mutex>
#include <system_error>

#if defined(LS_PLATFORM_WINDOWS)
	#include <Windows.h>
#else
	#include <dlfcn.h>
#endif

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

	namespace {

		// Loads a driver library and finds its entry point, the function the Vulkan loader calls drivers through
		std::expected<PFN_vkGetInstanceProcAddrLUNARG, Error> OpenDriver(const std::filesystem::path& library)
		{
			constexpr const char* entryPointName = "vk_icdGetInstanceProcAddr";
#if defined(LS_PLATFORM_WINDOWS)
			// The driver's own dependencies are searched for next to it
			HMODULE module = LoadLibraryExW(library.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
			if (module == nullptr)
				return std::unexpected(Error(ErrorCode::DeviceError,
					fmt::format("Can't load the Vulkan driver {}: {}", library,
						std::system_category().message(static_cast<int>(GetLastError())))));
			auto entryPoint = reinterpret_cast<PFN_vkGetInstanceProcAddrLUNARG>(GetProcAddress(module, entryPointName));
			if (entryPoint == nullptr)
				FreeLibrary(module);
#else
			void* module = dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
			if (module == nullptr)
			{
				// glibc and macOS keep dlerror()'s state per thread
				const char* reason = dlerror(); // NOLINT(concurrency-mt-unsafe)
				return std::unexpected(Error(ErrorCode::DeviceError,
					fmt::format(
						"Can't load the Vulkan driver {}: {}", library, reason != nullptr ? reason : "unknown")));
			}
			auto entryPoint = reinterpret_cast<PFN_vkGetInstanceProcAddrLUNARG>(dlsym(module, entryPointName));
			if (entryPoint == nullptr)
				dlclose(module);
#endif
			if (entryPoint == nullptr)
				return std::unexpected(Error(ErrorCode::DeviceError,
					fmt::format("{} isn't a Vulkan driver: it doesn't export {}", library, entryPointName)));
			return entryPoint;
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

	std::expected<PFN_vkGetInstanceProcAddrLUNARG, Error> LoadVulkanDriver(const std::filesystem::path& library)
	{
		std::error_code error;
		const std::filesystem::path absolute = std::filesystem::absolute(library, error);
		if (error)
			return std::unexpected(
				Error(ErrorCode::InvalidArgument, fmt::format("Invalid Vulkan driver path {}: {}", library, error)));

		// Drivers stay loaded until the process ends, like the Vulkan loader, so they're never unloaded under a device.
		// Intentionally never destroyed
		static auto* s_Mutex = new std::mutex();
		static auto* s_Drivers = new std::map<std::filesystem::path, PFN_vkGetInstanceProcAddrLUNARG>();
		const std::scoped_lock lock(*s_Mutex);
		if (const auto loaded = s_Drivers->find(absolute); loaded != s_Drivers->end())
			return loaded->second;

		auto entryPoint = OpenDriver(absolute);
		if (entryPoint)
			s_Drivers->emplace(absolute, *entryPoint);
		return entryPoint;
	}

}
