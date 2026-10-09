#pragma once

#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/Error.h"

#include <nvrhi/nvrhi.h>
#include <vulkan/vulkan_core.h>

#include <atomic>
#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace nvrhi::vulkan { // NOLINT(readability-identifier-naming): NVRHI's namespace
	class IDevice;
}

namespace Lodestone {

	namespace Detail {
		class NvrhiMessageCallback;
	}

	enum class GpuPreference
	{
		// Prefer a discrete GPU, then an integrated one
		HighPerformance,
		// Prefer an integrated GPU, then a discrete one
		LowPower,
	};

	enum class GpuType
	{
		Discrete,
		Integrated,
		Virtual,
		Cpu,
		Other,
	};

	std::string_view ToString(GpuType type);

	struct GraphicsDeviceConfig
	{
		// Instance extensions needed to present to windows (Window::GetRequiredVulkanInstanceExtensions()). With none,
		// and no SupportsPresentation, the device is headless: it renders offscreen only
		std::vector<std::string> PresentationExtensions;
		// Whether a queue family of a physical device can present to the window (Window::SupportsVulkanPresentation())
		std::function<bool(VkInstance, VkPhysicalDevice, uint32_t)> SupportsPresentation;
		// Vulkan's and NVRHI's validation layers, which report API misuse - on by default in Debug builds. The Vulkan
		// layers come with the Vulkan SDK; without it, a warning is logged and only NVRHI's layer runs
#if defined(LS_CONFIG_DEBUG)
		bool EnableValidation = true;
#else
		bool EnableValidation = false;
#endif
		GpuPreference Preference = GpuPreference::HighPerformance;
	};

	struct GraphicsDeviceInfo
	{
		std::string Name;
		GpuType Type = GpuType::Other;
		uint32_t VendorId = 0;
		uint32_t DeviceId = 0;
		// The Vulkan version the device supports, as VK_MAKE_API_VERSION
		uint32_t ApiVersion = 0;
		std::string DriverName;
		std::string DriverInfo;
	};

	// The GPU the engine renders with: a Vulkan device, wrapped in an NVRHI device for rendering.
	//
	// Vulkan functions are dispatched through one process-wide table, so only one graphics device exists at a time.
	// Create it on the main thread; NVRHI's rules for multi-threaded use apply to GetNvrhiDevice()
	class GraphicsDevice
	{
	private:
		struct Passkey
		{
			explicit Passkey() = default;
		};

	public:
		[[nodiscard]] static std::expected<Scope<GraphicsDevice>, Error> Create(const GraphicsDeviceConfig& config);

		explicit GraphicsDevice(Passkey passkey);
		~GraphicsDevice();

		GraphicsDevice(const GraphicsDevice&) = delete;
		GraphicsDevice& operator=(const GraphicsDevice&) = delete;
		GraphicsDevice(GraphicsDevice&&) = delete;
		GraphicsDevice& operator=(GraphicsDevice&&) = delete;

		// The device to render with. It's NVRHI's validation layer when validation is enabled
		nvrhi::IDevice* GetNvrhiDevice() const { return m_NvrhiDevice; }
		const GraphicsDeviceInfo& GetInfo() const { return m_Info; }
		bool CanPresent() const { return m_CanPresent; }

		// How many errors the validation layers have reported. Tests check that rendering produces none
		uint32_t GetValidationErrorCount() const { return m_ValidationErrorCount.load(); }

		// Vulkan objects, for the code below NVRHI (the swapchain)
		VkInstance GetVulkanInstance() const { return m_Instance; }
		VkPhysicalDevice GetVulkanPhysicalDevice() const { return m_PhysicalDevice; }
		VkDevice GetVulkanDevice() const { return m_Device; }
		VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
		uint32_t GetGraphicsQueueFamily() const { return m_GraphicsQueueFamily; }
		// The NVRHI Vulkan device without the validation layer, for Vulkan-specific NVRHI calls
		nvrhi::vulkan::IDevice* GetNvrhiVulkanDevice() const;

	private:
		[[nodiscard]] std::expected<void, Error> CreateInstance(const GraphicsDeviceConfig& config);
		[[nodiscard]] std::expected<void, Error> CreateDevice(const GraphicsDeviceConfig& config);
		[[nodiscard]] std::expected<void, Error> CreateNvrhiDevice(const GraphicsDeviceConfig& config);

	private:
		GraphicsDeviceInfo m_Info;
		bool m_CanPresent = false;
		std::atomic<uint32_t> m_ValidationErrorCount = 0;

		VkInstance m_Instance = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		VkDevice m_Device = VK_NULL_HANDLE;
		VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
		uint32_t m_GraphicsQueueFamily = 0;
		std::vector<std::string> m_InstanceExtensions;
		std::vector<std::string> m_DeviceExtensions;
		bool m_BufferDeviceAddress = false;

		Scope<Detail::NvrhiMessageCallback> m_MessageCallback;
		nvrhi::DeviceHandle m_VulkanNvrhiDevice;
		nvrhi::DeviceHandle m_NvrhiDevice;
	};

}
