#include "Lodestone/Graphics/GraphicsDevice.h"

#include "Lodestone/Core/Log.h"
#include "Lodestone/Core/Version.h"
#include "Lodestone/Graphics/Vulkan.h"
#include "Lodestone/Graphics/VulkanLibrary.h"

#include <nvrhi/validation.h>
#include <nvrhi/vulkan.h>

#include <algorithm>
#include <optional>
#include <span>

namespace Lodestone {

	namespace Detail {

		// Routes NVRHI's messages to the engine log. Errors count as validation errors
		class NvrhiMessageCallback final : public nvrhi::IMessageCallback
		{
		public:
			explicit NvrhiMessageCallback(std::atomic<uint32_t>& errorCount)
				: m_ErrorCount(&errorCount)
			{
			}

			~NvrhiMessageCallback() override = default;

			NvrhiMessageCallback(const NvrhiMessageCallback&) = delete;
			NvrhiMessageCallback& operator=(const NvrhiMessageCallback&) = delete;
			NvrhiMessageCallback(NvrhiMessageCallback&&) = delete;
			NvrhiMessageCallback& operator=(NvrhiMessageCallback&&) = delete;

			void message(nvrhi::MessageSeverity severity, const char* messageText) override
			{
				switch (severity)
				{
					case nvrhi::MessageSeverity::Info:
						LS_CORE_TRACE("NVRHI: {}", messageText);
						break;
					case nvrhi::MessageSeverity::Warning:
						LS_CORE_WARN("NVRHI: {}", messageText);
						break;
					case nvrhi::MessageSeverity::Error:
					case nvrhi::MessageSeverity::Fatal:
						++*m_ErrorCount;
						LS_CORE_ERROR("NVRHI: {}", messageText);
						break;
				}
			}

		private:
			std::atomic<uint32_t>* m_ErrorCount;
		};

	}

	namespace {

		// NVRHI renders with dynamic rendering and synchronization2, which are core in Vulkan 1.3
		constexpr uint32_t RequiredApiVersion = VK_API_VERSION_1_3;
		constexpr std::string_view ValidationLayerName = "VK_LAYER_KHRONOS_validation";
		// Defined only in the beta extension header, so spelled out
		constexpr std::string_view PortabilitySubsetExtensionName = "VK_KHR_portability_subset";

		std::atomic<bool> s_DeviceExists = false;

		std::string FormatVersion(uint32_t version)
		{
			return fmt::format("{}.{}.{}", VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version),
				VK_API_VERSION_PATCH(version));
		}

		// Calls a Vulkan enumeration function twice - for the count, then the items - until the list is complete
		template <typename T, typename Function>
		std::expected<std::vector<T>, VkResult> Enumerate(const Function& function)
		{
			std::vector<T> items;
			VkResult result = VK_INCOMPLETE;
			while (result == VK_INCOMPLETE)
			{
				uint32_t count = 0;
				result = function(&count, nullptr);
				if (result != VK_SUCCESS)
					return std::unexpected(result);
				items.resize(count);
				result = function(&count, items.data());
				items.resize(count);
			}
			if (result != VK_SUCCESS)
				return std::unexpected(result);
			return items;
		}

		bool HasExtension(std::span<const VkExtensionProperties> extensions, std::string_view name)
		{
			return std::ranges::any_of(extensions, [name](const VkExtensionProperties& extension)
				{ return name == static_cast<const char*>(extension.extensionName); });
		}

		bool HasLayer(std::span<const VkLayerProperties> layers, std::string_view name)
		{
			return std::ranges::any_of(layers,
				[name](const VkLayerProperties& layer) { return name == static_cast<const char*>(layer.layerName); });
		}

		std::vector<const char*> ToCStrings(const std::vector<std::string>& strings)
		{
			std::vector<const char*> result;
			result.reserve(strings.size());
			for (const std::string& string : strings)
				result.push_back(string.c_str());
			return result;
		}

		VKAPI_ATTR VkBool32 VKAPI_CALL OnDebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
			VkDebugUtilsMessageTypeFlagsEXT /*types*/, const VkDebugUtilsMessengerCallbackDataEXT* data, void* userData)
		{
			const char* message = data != nullptr && data->pMessage != nullptr ? data->pMessage : "(no message)";
			// The loader's own messages are about its search for drivers and layers, not about the engine's use of
			// Vulkan
			const bool fromLoader = data != nullptr && data->pMessageIdName != nullptr &&
				std::string_view(data->pMessageIdName) == "Loader Message";
			if (fromLoader)
			{
				LS_CORE_DEBUG("Vulkan loader: {}", message);
			}
			else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
			{
				++*static_cast<std::atomic<uint32_t>*>(userData);
				LS_CORE_ERROR("Vulkan: {}", message);
			}
			else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
			{
				LS_CORE_WARN("Vulkan: {}", message);
			}
			else
			{
				LS_CORE_TRACE("Vulkan: {}", message);
			}
			// The call that triggered the message continues as normal
			return VK_FALSE;
		}

		VkDebugUtilsMessengerCreateInfoEXT MakeDebugMessengerInfo(std::atomic<uint32_t>& errorCount)
		{
			VkDebugUtilsMessengerCreateInfoEXT info{};
			info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			info.messageSeverity =
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			info.pfnUserCallback = &OnDebugMessage;
			info.pUserData = &errorCount;
			return info;
		}

		GpuType ToGpuType(VkPhysicalDeviceType type)
		{
			switch (type)
			{
				case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
					return GpuType::Discrete;
				case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
					return GpuType::Integrated;
				case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
					return GpuType::Virtual;
				case VK_PHYSICAL_DEVICE_TYPE_CPU:
					return GpuType::Cpu;
				default:
					return GpuType::Other;
			}
		}

		// Higher is better
		int GetPreferenceScore(GpuType type, GpuPreference preference)
		{
			switch (type)
			{
				case GpuType::Discrete:
					return preference == GpuPreference::HighPerformance ? 4 : 3;
				case GpuType::Integrated:
					return preference == GpuPreference::HighPerformance ? 3 : 4;
				case GpuType::Virtual:
					return 2;
				case GpuType::Cpu:
					return 1;
				case GpuType::Other:
					return 0;
			}
			return 0;
		}

		struct DeviceCandidate
		{
			VkPhysicalDevice Handle = VK_NULL_HANDLE;
			VkPhysicalDeviceProperties Properties{};
			VkPhysicalDeviceFeatures CoreFeatures{};
			uint32_t GraphicsQueueFamily = 0;
			bool HasPortabilitySubset = false;
			bool HasBufferDeviceAddress = false;
			bool HasMaintenance4 = false;
		};

		// Checks that a physical device can run Lodestone, or explains why it can't
		std::expected<DeviceCandidate, std::string> EvaluateDevice(
			VkInstance instance, VkPhysicalDevice handle, const GraphicsDeviceConfig& config)
		{
			auto& dispatch = Vulkan::Dispatch();
			DeviceCandidate candidate;
			candidate.Handle = handle;
			dispatch.vkGetPhysicalDeviceProperties(handle, &candidate.Properties);
			if (candidate.Properties.apiVersion < RequiredApiVersion)
				return std::unexpected(fmt::format(
					"it supports Vulkan {}, but Lodestone needs 1.3", FormatVersion(candidate.Properties.apiVersion)));

			VkPhysicalDeviceVulkan13Features features13{};
			features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
			VkPhysicalDeviceVulkan12Features features12{};
			features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
			features12.pNext = &features13;
			VkPhysicalDeviceFeatures2 features{};
			features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
			features.pNext = &features12;
			dispatch.vkGetPhysicalDeviceFeatures2(handle, &features);
			if (features12.timelineSemaphore == VK_FALSE)
				return std::unexpected("it doesn't support timeline semaphores");
			if (features13.dynamicRendering == VK_FALSE)
				return std::unexpected("it doesn't support dynamic rendering");
			if (features13.synchronization2 == VK_FALSE)
				return std::unexpected("it doesn't support synchronization2");
			candidate.CoreFeatures = features.features;
			candidate.HasBufferDeviceAddress = features12.bufferDeviceAddress == VK_TRUE;
			candidate.HasMaintenance4 = features13.maintenance4 == VK_TRUE;

			const auto extensions = Enumerate<VkExtensionProperties>([&](uint32_t* count, VkExtensionProperties* data)
				{ return dispatch.vkEnumerateDeviceExtensionProperties(handle, nullptr, count, data); });
			if (!extensions)
				return std::unexpected(fmt::format(
					"listing its extensions failed: {}", nvrhi::vulkan::resultToString(extensions.error())));
			candidate.HasPortabilitySubset = HasExtension(*extensions, PortabilitySubsetExtensionName);

			const bool presenting = static_cast<bool>(config.SupportsPresentation);
			if (presenting && !HasExtension(*extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
				return std::unexpected("it can't present to windows (no VK_KHR_swapchain)");

			uint32_t familyCount = 0;
			dispatch.vkGetPhysicalDeviceQueueFamilyProperties(handle, &familyCount, nullptr);
			std::vector<VkQueueFamilyProperties> families(familyCount);
			dispatch.vkGetPhysicalDeviceQueueFamilyProperties(handle, &familyCount, families.data());
			for (uint32_t family = 0; family < familyCount; ++family)
			{
				if ((families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
					continue;
				if (presenting && !config.SupportsPresentation(instance, handle, family))
					continue;
				candidate.GraphicsQueueFamily = family;
				return candidate;
			}
			return std::unexpected(
				presenting ? "no queue family can both draw and present" : "no queue family can draw");
		}

		// The optional core features Lodestone turns on when the device has them
		VkPhysicalDeviceFeatures SelectCoreFeatures(const VkPhysicalDeviceFeatures& supported)
		{
			VkPhysicalDeviceFeatures features{};
			features.samplerAnisotropy = supported.samplerAnisotropy;
			features.fillModeNonSolid = supported.fillModeNonSolid;
			features.independentBlend = supported.independentBlend;
			features.depthClamp = supported.depthClamp;
			features.imageCubeArray = supported.imageCubeArray;
			features.textureCompressionBC = supported.textureCompressionBC;
			features.multiDrawIndirect = supported.multiDrawIndirect;
			features.drawIndirectFirstInstance = supported.drawIndirectFirstInstance;
			features.shaderInt16 = supported.shaderInt16;
			return features;
		}

	}

	std::string_view ToString(GpuType type)
	{
		switch (type)
		{
			case GpuType::Discrete:
				return "Discrete GPU";
			case GpuType::Integrated:
				return "Integrated GPU";
			case GpuType::Virtual:
				return "Virtual GPU";
			case GpuType::Cpu:
				return "CPU";
			case GpuType::Other:
				return "Other";
		}
		return "Unknown";
	}

	std::expected<Scope<GraphicsDevice>, Error> GraphicsDevice::Create(const GraphicsDeviceConfig& config)
	{
		if (config.PresentationExtensions.empty() != !config.SupportsPresentation)
			return std::unexpected(Error(ErrorCode::InvalidArgument,
				"PresentationExtensions and SupportsPresentation must be set together, or both left empty for a "
				"headless device"));
		if (s_DeviceExists.exchange(true))
			return std::unexpected(Error(ErrorCode::InvalidState, "Only one graphics device can exist at a time"));

		// From here on, the device's destructor cleans up whatever was created
		auto device = CreateScope<GraphicsDevice>(Passkey());
		if (auto library = LoadVulkanLibrary(); !library)
			return std::unexpected(std::move(library).error());
		if (auto created = device->CreateInstance(config); !created)
			return std::unexpected(std::move(created).error().WithContext("Creating the graphics device"));
		if (auto created = device->CreateDevice(config); !created)
			return std::unexpected(std::move(created).error().WithContext("Creating the graphics device"));
		if (auto created = device->CreateNvrhiDevice(config); !created)
			return std::unexpected(std::move(created).error().WithContext("Creating the graphics device"));

		const GraphicsDeviceInfo& info = device->m_Info;
		LS_CORE_INFO("Graphics device: {} ({}, Vulkan {}, driver {} {}){}", info.Name, ToString(info.Type),
			FormatVersion(info.ApiVersion), info.DriverName, info.DriverInfo,
			config.EnableValidation ? " with validation" : "");
		return device;
	}

	GraphicsDevice::GraphicsDevice(Passkey /*passkey*/)
	{
	}

	GraphicsDevice::~GraphicsDevice()
	{
		if (m_NvrhiDevice)
		{
			m_NvrhiDevice->waitForIdle();
			m_NvrhiDevice->runGarbageCollection();
		}
		// NVRHI's objects go before the Vulkan device they were made from
		m_NvrhiDevice = nullptr;
		m_VulkanNvrhiDevice = nullptr;
		m_MessageCallback.reset();

		auto& dispatch = Vulkan::Dispatch();
		if (m_Device != VK_NULL_HANDLE)
			dispatch.vkDestroyDevice(m_Device, nullptr);
		if (m_DebugMessenger != VK_NULL_HANDLE)
			dispatch.vkDestroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessenger, nullptr);
		if (m_Instance != VK_NULL_HANDLE)
			dispatch.vkDestroyInstance(m_Instance, nullptr);

		s_DeviceExists = false;
	}

	nvrhi::vulkan::IDevice* GraphicsDevice::GetNvrhiVulkanDevice() const
	{
		return static_cast<nvrhi::vulkan::IDevice*>(m_VulkanNvrhiDevice.Get());
	}

	std::expected<void, Error> GraphicsDevice::CreateInstance(const GraphicsDeviceConfig& config)
	{
		auto& dispatch = Vulkan::Dispatch();

		uint32_t loaderVersion = VK_API_VERSION_1_0;
		if (dispatch.vkEnumerateInstanceVersion != nullptr)
			dispatch.vkEnumerateInstanceVersion(&loaderVersion);
		if (loaderVersion < RequiredApiVersion)
			return std::unexpected(Error(ErrorCode::DeviceError,
				fmt::format("The Vulkan loader supports Vulkan {}, but Lodestone needs 1.3. Update the graphics driver",
					FormatVersion(loaderVersion))));

		const auto availableExtensions =
			Enumerate<VkExtensionProperties>([&](uint32_t* count, VkExtensionProperties* data)
				{ return dispatch.vkEnumerateInstanceExtensionProperties(nullptr, count, data); });
		if (!availableExtensions)
			return std::unexpected(
				Vulkan::MakeError("Listing Vulkan instance extensions", availableExtensions.error()));
		const auto availableLayers = Enumerate<VkLayerProperties>([&](uint32_t* count, VkLayerProperties* data)
			{ return dispatch.vkEnumerateInstanceLayerProperties(count, data); });
		if (!availableLayers)
			return std::unexpected(Vulkan::MakeError("Listing Vulkan layers", availableLayers.error()));

		for (const std::string& extension : config.PresentationExtensions)
		{
			if (!HasExtension(*availableExtensions, extension))
				return std::unexpected(Error(ErrorCode::DeviceError,
					fmt::format("The Vulkan instance extension {} isn't available", extension)));
			m_InstanceExtensions.push_back(extension);
		}

		std::vector<const char*> layers;
		bool debugUtils = false;
		if (config.EnableValidation)
		{
			if (HasLayer(*availableLayers, ValidationLayerName))
				layers.push_back(ValidationLayerName.data());
			else
				LS_CORE_WARN("The Vulkan validation layers aren't installed, so Vulkan API misuse won't be reported. "
							 "They come with the Vulkan SDK");

			if (HasExtension(*availableExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
			{
				m_InstanceExtensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
				debugUtils = true;
			}
		}

		// Drivers that don't fully conform to Vulkan, such as MoltenVK on macOS, are only listed on request
		VkInstanceCreateFlags flags = 0;
		if (HasExtension(*availableExtensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
		{
			m_InstanceExtensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
			flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
		}

		VkApplicationInfo application{};
		application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		application.pApplicationName = "Lodestone";
		application.applicationVersion = VK_MAKE_API_VERSION(0, VersionMajor, VersionMinor, VersionPatch);
		application.pEngineName = "Lodestone";
		application.engineVersion = application.applicationVersion;
		application.apiVersion = RequiredApiVersion;

		const std::vector<const char*> extensions = ToCStrings(m_InstanceExtensions);
		VkDebugUtilsMessengerCreateInfoEXT messengerInfo = MakeDebugMessengerInfo(m_ValidationErrorCount);

		VkInstanceCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		// Also reports problems in vkCreateInstance and vkDestroyInstance, which the messenger can't see
		info.pNext = debugUtils ? &messengerInfo : nullptr;
		info.flags = flags;
		info.pApplicationInfo = &application;
		info.enabledLayerCount = static_cast<uint32_t>(layers.size());
		info.ppEnabledLayerNames = layers.data();
		info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
		info.ppEnabledExtensionNames = extensions.data();

		if (const VkResult result = dispatch.vkCreateInstance(&info, nullptr, &m_Instance); result != VK_SUCCESS)
			return std::unexpected(Vulkan::MakeError("Creating the Vulkan instance", result));
		dispatch.init(vk::Instance(m_Instance));

		if (debugUtils)
		{
			if (const VkResult result =
					dispatch.vkCreateDebugUtilsMessengerEXT(m_Instance, &messengerInfo, nullptr, &m_DebugMessenger);
				result != VK_SUCCESS)
				return std::unexpected(Vulkan::MakeError("Creating the Vulkan debug messenger", result));
		}
		return {};
	}

	std::expected<void, Error> GraphicsDevice::CreateDevice(const GraphicsDeviceConfig& config)
	{
		auto& dispatch = Vulkan::Dispatch();

		const auto physicalDevices = Enumerate<VkPhysicalDevice>([&](uint32_t* count, VkPhysicalDevice* data)
			{ return dispatch.vkEnumeratePhysicalDevices(m_Instance, count, data); });
		if (!physicalDevices)
			return std::unexpected(Vulkan::MakeError("Listing Vulkan devices", physicalDevices.error()));
		if (physicalDevices->empty())
			return std::unexpected(Error(ErrorCode::DeviceError,
				"No Vulkan devices were found. Install a graphics driver that supports Vulkan"));

		std::optional<DeviceCandidate> best;
		std::string rejections;
		for (VkPhysicalDevice physicalDevice : *physicalDevices)
		{
			auto candidate = EvaluateDevice(m_Instance, physicalDevice, config);
			if (!candidate)
			{
				VkPhysicalDeviceProperties properties{};
				dispatch.vkGetPhysicalDeviceProperties(physicalDevice, &properties);
				rejections +=
					fmt::format("\n  {}: {}", static_cast<const char*>(properties.deviceName), candidate.error());
				continue;
			}
			const auto score = [&config](const DeviceCandidate& device)
			{ return GetPreferenceScore(ToGpuType(device.Properties.deviceType), config.Preference); };
			if (!best || score(*candidate) > score(*best))
				best = *candidate;
		}
		if (!best)
			return std::unexpected(
				Error(ErrorCode::DeviceError, fmt::format("No Vulkan device can run Lodestone:{}", rejections)));

		m_PhysicalDevice = best->Handle;
		m_GraphicsQueueFamily = best->GraphicsQueueFamily;
		m_CanPresent = static_cast<bool>(config.SupportsPresentation);
		m_BufferDeviceAddress = best->HasBufferDeviceAddress;

		if (m_CanPresent)
			m_DeviceExtensions.emplace_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
		// Required whenever the device has it
		if (best->HasPortabilitySubset)
			m_DeviceExtensions.emplace_back(PortabilitySubsetExtensionName);

		VkPhysicalDeviceVulkan13Features features13{};
		features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
		features13.dynamicRendering = VK_TRUE;
		features13.synchronization2 = VK_TRUE;
		features13.maintenance4 = best->HasMaintenance4 ? VK_TRUE : VK_FALSE;
		VkPhysicalDeviceVulkan12Features features12{};
		features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		features12.pNext = &features13;
		features12.timelineSemaphore = VK_TRUE;
		features12.bufferDeviceAddress = m_BufferDeviceAddress ? VK_TRUE : VK_FALSE;
		VkPhysicalDeviceFeatures2 features{};
		features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		features.pNext = &features12;
		features.features = SelectCoreFeatures(best->CoreFeatures);

		const float queuePriority = 1.0f;
		VkDeviceQueueCreateInfo queueInfo{};
		queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueInfo.queueFamilyIndex = m_GraphicsQueueFamily;
		queueInfo.queueCount = 1;
		queueInfo.pQueuePriorities = &queuePriority;

		const std::vector<const char*> extensions = ToCStrings(m_DeviceExtensions);
		VkDeviceCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		info.pNext = &features;
		info.queueCreateInfoCount = 1;
		info.pQueueCreateInfos = &queueInfo;
		info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
		info.ppEnabledExtensionNames = extensions.data();

		if (const VkResult result = dispatch.vkCreateDevice(m_PhysicalDevice, &info, nullptr, &m_Device);
			result != VK_SUCCESS)
			return std::unexpected(Vulkan::MakeError("Creating the Vulkan device", result));
		dispatch.init(vk::Device(m_Device));
		dispatch.vkGetDeviceQueue(m_Device, m_GraphicsQueueFamily, 0, &m_GraphicsQueue);

		VkPhysicalDeviceDriverProperties driver{};
		driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
		VkPhysicalDeviceProperties2 properties{};
		properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		properties.pNext = &driver;
		dispatch.vkGetPhysicalDeviceProperties2(m_PhysicalDevice, &properties);

		m_Info.Name = static_cast<const char*>(properties.properties.deviceName);
		m_Info.Type = ToGpuType(properties.properties.deviceType);
		m_Info.VendorId = properties.properties.vendorID;
		m_Info.DeviceId = properties.properties.deviceID;
		m_Info.ApiVersion = properties.properties.apiVersion;
		m_Info.DriverName = static_cast<const char*>(driver.driverName);
		m_Info.DriverInfo = static_cast<const char*>(driver.driverInfo);
		return {};
	}

	std::expected<void, Error> GraphicsDevice::CreateNvrhiDevice(const GraphicsDeviceConfig& config)
	{
		m_MessageCallback = CreateScope<Detail::NvrhiMessageCallback>(m_ValidationErrorCount);

		const std::vector<const char*> instanceExtensions = ToCStrings(m_InstanceExtensions);
		const std::vector<const char*> deviceExtensions = ToCStrings(m_DeviceExtensions);

		nvrhi::vulkan::DeviceDesc desc;
		desc.errorCB = m_MessageCallback.get();
		desc.instance = m_Instance;
		desc.physicalDevice = m_PhysicalDevice;
		desc.device = m_Device;
		desc.graphicsQueue = m_GraphicsQueue;
		desc.graphicsQueueIndex = static_cast<int>(m_GraphicsQueueFamily);
		desc.instanceExtensions = const_cast<const char**>(instanceExtensions.data());
		desc.numInstanceExtensions = instanceExtensions.size();
		desc.deviceExtensions = const_cast<const char**>(deviceExtensions.data());
		desc.numDeviceExtensions = deviceExtensions.size();
		desc.bufferDeviceAddressSupported = m_BufferDeviceAddress;

		m_VulkanNvrhiDevice = nvrhi::vulkan::createDevice(desc);
		if (!m_VulkanNvrhiDevice)
			return std::unexpected(Error(ErrorCode::DeviceError, "Creating the NVRHI device failed (see the log)"));
		m_NvrhiDevice = config.EnableValidation ? nvrhi::validation::createValidationLayer(m_VulkanNvrhiDevice)
												: m_VulkanNvrhiDevice;
		return {};
	}

}
