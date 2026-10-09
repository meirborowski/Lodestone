#include "Lodestone/Graphics/Swapchain.h"

#include "Lodestone/Core/Log.h"
#include "Lodestone/Graphics/GraphicsDevice.h"
#include "Lodestone/Graphics/Vulkan.h"
#include "Lodestone/Platform/Window.h"

#include <nvrhi/vulkan.h>

#include <algorithm>
#include <array>
#include <limits>

namespace Lodestone {

	namespace {

		struct FormatChoice
		{
			VkFormat VulkanFormat;
			nvrhi::Format NvrhiFormat;
		};

		// 8-bit formats every desktop driver offers for presentation, in order of preference. The renderer writes
		// display-ready values, so the formats don't convert to sRGB on write
		constexpr std::array PreferredFormats = {
			FormatChoice{.VulkanFormat = VK_FORMAT_B8G8R8A8_UNORM, .NvrhiFormat = nvrhi::Format::BGRA8_UNORM},
			FormatChoice{.VulkanFormat = VK_FORMAT_R8G8B8A8_UNORM, .NvrhiFormat = nvrhi::Format::RGBA8_UNORM},
		};

		std::expected<FormatChoice, Error> ChooseFormat(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
		{
			auto& dispatch = Vulkan::Dispatch();
			uint32_t count = 0;
			if (const VkResult result =
					dispatch.vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, nullptr);
				result != VK_SUCCESS)
				return std::unexpected(Vulkan::MakeError("Listing the window's surface formats", result));
			std::vector<VkSurfaceFormatKHR> formats(count);
			if (const VkResult result =
					dispatch.vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, formats.data());
				result != VK_SUCCESS && result != VK_INCOMPLETE)
				return std::unexpected(Vulkan::MakeError("Listing the window's surface formats", result));

			for (const FormatChoice& preferred : PreferredFormats)
			{
				const bool supported = std::ranges::any_of(formats,
					[&preferred](const VkSurfaceFormatKHR& format)
					{
						return format.format == preferred.VulkanFormat &&
							format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
					});
				if (supported)
					return preferred;
			}
			return std::unexpected(
				Error(ErrorCode::DeviceError, "The window supports no 8-bit BGRA or RGBA presentation format"));
		}

		std::expected<VkPresentModeKHR, Error> ChoosePresentMode(
			VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, bool vsync)
		{
			// FIFO is always available, and waits for the vertical blank
			if (vsync)
				return VK_PRESENT_MODE_FIFO_KHR;

			auto& dispatch = Vulkan::Dispatch();
			uint32_t count = 0;
			if (const VkResult result =
					dispatch.vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, nullptr);
				result != VK_SUCCESS)
				return std::unexpected(Vulkan::MakeError("Listing the window's present modes", result));
			std::vector<VkPresentModeKHR> modes(count);
			if (const VkResult result =
					dispatch.vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, modes.data());
				result != VK_SUCCESS && result != VK_INCOMPLETE)
				return std::unexpected(Vulkan::MakeError("Listing the window's present modes", result));

			for (const VkPresentModeKHR preferred : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR})
			{
				if (std::ranges::find(modes, preferred) != modes.end())
					return preferred;
			}
			return VK_PRESENT_MODE_FIFO_KHR;
		}

	}

	std::expected<Scope<Swapchain>, Error> Swapchain::Create(
		GraphicsDevice& device, const Window& window, const SwapchainConfig& config)
	{
		if (!device.CanPresent())
			return std::unexpected(
				Error(ErrorCode::InvalidArgument, "A headless graphics device can't present to a window"));

		auto swapchain = CreateScope<Swapchain>(Passkey(), device, window, config);
		if (auto created = swapchain->CreateSurface(); !created)
			return std::unexpected(std::move(created).error().WithContext("Creating the swapchain"));
		if (auto created = swapchain->CreateSemaphores(); !created)
			return std::unexpected(std::move(created).error().WithContext("Creating the swapchain"));
		if (auto created = swapchain->CreateSwapchain(); !created)
			return std::unexpected(std::move(created).error().WithContext("Creating the swapchain"));
		return swapchain;
	}

	Swapchain::Swapchain(
		Passkey /*passkey*/, GraphicsDevice& device, const Window& window, const SwapchainConfig& config)
		: m_Device(&device), m_Window(&window), m_Config(config)
	{
	}

	Swapchain::~Swapchain()
	{
		m_Device->GetNvrhiDevice()->waitForIdle();
		m_FramesInFlight.clear();
		m_FreeQueries.clear();
		DestroySwapchainResources();

		auto& dispatch = Vulkan::Dispatch();
		const VkDevice device = m_Device->GetVulkanDevice();
		for (const VkSemaphore semaphore : m_AcquireSemaphores)
			dispatch.vkDestroySemaphore(device, semaphore, nullptr);
		for (const VkSemaphore semaphore : m_PresentSemaphores)
			dispatch.vkDestroySemaphore(device, semaphore, nullptr);
		if (m_Swapchain != VK_NULL_HANDLE)
			dispatch.vkDestroySwapchainKHR(device, m_Swapchain, nullptr);
		if (m_Surface != VK_NULL_HANDLE)
			dispatch.vkDestroySurfaceKHR(m_Device->GetVulkanInstance(), m_Surface, nullptr);
	}

	std::expected<bool, Error> Swapchain::BeginFrame()
	{
		if (m_Window->IsMinimized())
			return false;

		if (m_NeedsRebuild || m_Window->GetFramebufferSize() != m_Size)
		{
			if (auto rebuilt = CreateSwapchain(); !rebuilt)
				return std::unexpected(std::move(rebuilt).error().WithContext("Resizing the swapchain"));
			// The surface has no size yet (the window is being minimized), so there are no images
			if (m_Textures.empty())
				return false;
		}

		auto& dispatch = Vulkan::Dispatch();
		// An out-of-date swapchain is rebuilt and the image acquired again, once
		for (int attempt = 0; attempt < 2; ++attempt)
		{
			const VkSemaphore acquired = m_AcquireSemaphores[m_AcquireSemaphoreIndex];
			const VkResult result = dispatch.vkAcquireNextImageKHR(m_Device->GetVulkanDevice(), m_Swapchain,
				std::numeric_limits<uint64_t>::max(), acquired, VK_NULL_HANDLE, &m_ImageIndex);

			if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)
			{
				// A suboptimal swapchain still works; it's rebuilt after this frame
				m_NeedsRebuild = result == VK_SUBOPTIMAL_KHR;
				m_AcquireSemaphoreIndex =
					(m_AcquireSemaphoreIndex + 1) % static_cast<uint32_t>(m_AcquireSemaphores.size());
				// The frame's first submission waits until the image is ready
				m_Device->GetNvrhiVulkanDevice()->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, acquired, 0);
				return true;
			}

			if (result != VK_ERROR_OUT_OF_DATE_KHR)
				return std::unexpected(Vulkan::MakeError("Acquiring a swapchain image", result));
			if (auto rebuilt = CreateSwapchain(); !rebuilt)
				return std::unexpected(std::move(rebuilt).error().WithContext("Rebuilding the swapchain"));
			if (m_Textures.empty())
				return false;
		}
		return std::unexpected(Error(ErrorCode::DeviceError, "The swapchain stayed out of date after being rebuilt"));
	}

	std::expected<void, Error> Swapchain::Present()
	{
		nvrhi::IDevice* device = m_Device->GetNvrhiDevice();
		nvrhi::vulkan::IDevice* vulkanDevice = m_Device->GetNvrhiVulkanDevice();
		const VkSemaphore rendered = m_PresentSemaphores[m_ImageIndex];
		vulkanDevice->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, rendered, 0);
		// An empty submission, which carries the semaphore operations queued above. It goes to the Vulkan device
		// directly: NVRHI's validation layer drops submissions without command lists
		vulkanDevice->executeCommandLists(nullptr, 0);

		VkPresentInfoKHR info{};
		info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		info.waitSemaphoreCount = 1;
		info.pWaitSemaphores = &rendered;
		info.swapchainCount = 1;
		info.pSwapchains = &m_Swapchain;
		info.pImageIndices = &m_ImageIndex;
		const VkResult result = Vulkan::Dispatch().vkQueuePresentKHR(m_Device->GetGraphicsQueue(), &info);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
			m_NeedsRebuild = true;
		else if (result != VK_SUCCESS)
			return std::unexpected(Vulkan::MakeError("Presenting a swapchain image", result));

		LimitFramesInFlight();
		device->runGarbageCollection();
		return {};
	}

	nvrhi::FramebufferInfo Swapchain::GetFramebufferInfo() const
	{
		return nvrhi::FramebufferInfo().addColorFormat(m_Format);
	}

	std::expected<void, Error> Swapchain::CreateSurface()
	{
		auto surface = m_Window->CreateVulkanSurface(m_Device->GetVulkanInstance());
		if (!surface)
			return std::unexpected(std::move(surface).error());
		m_Surface = *surface;

		VkBool32 supported = VK_FALSE;
		if (const VkResult result = Vulkan::Dispatch().vkGetPhysicalDeviceSurfaceSupportKHR(
				m_Device->GetVulkanPhysicalDevice(), m_Device->GetGraphicsQueueFamily(), m_Surface, &supported);
			result != VK_SUCCESS)
			return std::unexpected(Vulkan::MakeError("Checking presentation support", result));
		if (supported == VK_FALSE)
			return std::unexpected(Error(ErrorCode::DeviceError, "The graphics queue can't present to the window"));

		const auto format = ChooseFormat(m_Device->GetVulkanPhysicalDevice(), m_Surface);
		if (!format)
			return std::unexpected(format.error());
		m_VulkanFormat = format->VulkanFormat;
		m_Format = format->NvrhiFormat;
		return {};
	}

	std::expected<void, Error> Swapchain::CreateSemaphores()
	{
		auto& dispatch = Vulkan::Dispatch();
		VkSemaphoreCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		for (uint32_t i = 0; i < MaxFramesInFlight + 1; ++i)
		{
			VkSemaphore semaphore = VK_NULL_HANDLE;
			if (const VkResult result =
					dispatch.vkCreateSemaphore(m_Device->GetVulkanDevice(), &info, nullptr, &semaphore);
				result != VK_SUCCESS)
				return std::unexpected(Vulkan::MakeError("Creating a semaphore", result));
			m_AcquireSemaphores.push_back(semaphore);
		}
		return {};
	}

	std::expected<void, Error> Swapchain::CreateSwapchain()
	{
		auto& dispatch = Vulkan::Dispatch();
		nvrhi::IDevice* nvrhiDevice = m_Device->GetNvrhiDevice();
		const VkDevice device = m_Device->GetVulkanDevice();
		const VkPhysicalDevice physicalDevice = m_Device->GetVulkanPhysicalDevice();

		// Nothing may still use the old images
		nvrhiDevice->waitForIdle();
		DestroySwapchainResources();

		VkSurfaceCapabilitiesKHR capabilities{};
		if (const VkResult result =
				dispatch.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, m_Surface, &capabilities);
			result != VK_SUCCESS)
			return std::unexpected(Vulkan::MakeError("Reading the window's surface capabilities", result));

		const auto presentMode = ChoosePresentMode(physicalDevice, m_Surface, m_Config.VSync);
		if (!presentMode)
			return std::unexpected(presentMode.error());

		// The surface dictates the size, unless it lets the swapchain choose (then it follows the window)
		VkExtent2D extent = capabilities.currentExtent;
		if (extent.width == std::numeric_limits<uint32_t>::max())
		{
			const glm::uvec2 size = m_Window->GetFramebufferSize();
			extent.width = std::clamp(size.x, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
			extent.height = std::clamp(size.y, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
		}
		if (extent.width == 0 || extent.height == 0)
		{
			// Minimized: there's nothing to create until the window has a size again
			m_Size = glm::uvec2(0);
			m_NeedsRebuild = true;
			return {};
		}

		// One more image than the minimum, so the CPU rarely waits for the presentation engine
		uint32_t imageCount = std::max(capabilities.minImageCount + 1, MaxFramesInFlight + 1);
		if (capabilities.maxImageCount != 0)
			imageCount = std::min(imageCount, capabilities.maxImageCount);

		VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		// Copies to and from the images, for clears, blits and screenshots, where the surface allows them
		usage |= capabilities.supportedUsageFlags & (VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

		VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		if ((capabilities.supportedCompositeAlpha & compositeAlpha) == 0)
			compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;

		const VkSwapchainKHR oldSwapchain = m_Swapchain;
		VkSwapchainCreateInfoKHR info{};
		info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		info.surface = m_Surface;
		info.minImageCount = imageCount;
		info.imageFormat = m_VulkanFormat;
		info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		info.imageExtent = extent;
		info.imageArrayLayers = 1;
		info.imageUsage = usage;
		info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		info.preTransform = capabilities.currentTransform;
		info.compositeAlpha = compositeAlpha;
		info.presentMode = *presentMode;
		info.clipped = VK_TRUE;
		info.oldSwapchain = oldSwapchain;

		VkSwapchainKHR swapchain = VK_NULL_HANDLE;
		const VkResult created = dispatch.vkCreateSwapchainKHR(device, &info, nullptr, &swapchain);
		if (oldSwapchain != VK_NULL_HANDLE)
			dispatch.vkDestroySwapchainKHR(device, oldSwapchain, nullptr);
		m_Swapchain = VK_NULL_HANDLE;
		if (created != VK_SUCCESS)
			return std::unexpected(Vulkan::MakeError("Creating the Vulkan swapchain", created));
		m_Swapchain = swapchain;

		uint32_t count = 0;
		dispatch.vkGetSwapchainImagesKHR(device, m_Swapchain, &count, nullptr);
		std::vector<VkImage> images(count);
		if (const VkResult result = dispatch.vkGetSwapchainImagesKHR(device, m_Swapchain, &count, images.data());
			result != VK_SUCCESS)
			return std::unexpected(Vulkan::MakeError("Getting the swapchain images", result));

		m_Size = glm::uvec2(extent.width, extent.height);
		for (const VkImage image : images)
		{
			nvrhi::TextureDesc desc;
			desc.width = extent.width;
			desc.height = extent.height;
			desc.format = m_Format;
			desc.debugName = "Swapchain image";
			desc.isShaderResource = false;
			desc.isRenderTarget = true;
			desc.initialState = nvrhi::ResourceStates::Present;
			desc.keepInitialState = true;
			nvrhi::TextureHandle texture =
				nvrhiDevice->createHandleForNativeTexture(nvrhi::ObjectTypes::VK_Image, nvrhi::Object(image), desc);
			m_Framebuffers.push_back(
				nvrhiDevice->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(texture)));
			m_Textures.push_back(std::move(texture));
		}

		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		for (size_t i = 0; i < images.size(); ++i)
		{
			VkSemaphore semaphore = VK_NULL_HANDLE;
			if (const VkResult result = dispatch.vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore);
				result != VK_SUCCESS)
				return std::unexpected(Vulkan::MakeError("Creating a semaphore", result));
			m_PresentSemaphores.push_back(semaphore);
		}

		m_ImageIndex = 0;
		m_NeedsRebuild = false;
		LS_CORE_DEBUG("Swapchain: {}x{}, {} images, {}", extent.width, extent.height, images.size(),
			*presentMode == VK_PRESENT_MODE_FIFO_KHR ? "vsync" : "no vsync");
		return {};
	}

	void Swapchain::DestroySwapchainResources()
	{
		m_Framebuffers.clear();
		m_Textures.clear();
		const VkDevice device = m_Device->GetVulkanDevice();
		for (const VkSemaphore semaphore : m_PresentSemaphores)
			Vulkan::Dispatch().vkDestroySemaphore(device, semaphore, nullptr);
		m_PresentSemaphores.clear();
	}

	void Swapchain::LimitFramesInFlight()
	{
		nvrhi::IDevice* device = m_Device->GetNvrhiDevice();
		nvrhi::EventQueryHandle query;
		if (m_FreeQueries.empty())
		{
			query = device->createEventQuery();
		}
		else
		{
			query = std::move(m_FreeQueries.back());
			m_FreeQueries.pop_back();
		}
		device->resetEventQuery(query);
		device->setEventQuery(query, nvrhi::CommandQueue::Graphics);
		m_FramesInFlight.push_back(std::move(query));

		while (m_FramesInFlight.size() > MaxFramesInFlight)
		{
			nvrhi::EventQueryHandle oldest = std::move(m_FramesInFlight.front());
			m_FramesInFlight.pop_front();
			device->waitEventQuery(oldest);
			m_FreeQueries.push_back(std::move(oldest));
		}
	}

}
