#pragma once

#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/Error.h"

#include <glm/vec2.hpp>
#include <nvrhi/nvrhi.h>
#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <deque>
#include <expected>
#include <vector>

namespace Lodestone {

	class GraphicsDevice;
	class Window;

	struct SwapchainConfig
	{
		// Wait for the display's vertical blank, so frames never tear and the frame rate matches the display
		bool VSync = true;
	};

	// The images a window displays. Each frame: BeginFrame(), render into GetCurrentFramebuffer() and execute the
	// command lists, then Present(). The swapchain follows the window's size, and is rebuilt when it changes.
	// The device and the window must outlive the swapchain
	class Swapchain
	{
	private:
		struct Passkey
		{
			explicit Passkey() = default;
		};

	public:
		// How many frames the CPU may queue ahead of the GPU
		static constexpr uint32_t MaxFramesInFlight = 2;

		[[nodiscard]] static std::expected<Scope<Swapchain>, Error> Create(
			GraphicsDevice& device, const Window& window, const SwapchainConfig& config = {});

		Swapchain(Passkey passkey, GraphicsDevice& device, const Window& window, const SwapchainConfig& config);
		~Swapchain();

		Swapchain(const Swapchain&) = delete;
		Swapchain& operator=(const Swapchain&) = delete;
		Swapchain(Swapchain&&) = delete;
		Swapchain& operator=(Swapchain&&) = delete;

		// Acquires the image to render this frame. Returns false when there's nothing to render into - the window is
		// minimized - and the frame should be skipped
		[[nodiscard]] std::expected<bool, Error> BeginFrame();
		// Shows the image, after the frame's command lists have been executed
		[[nodiscard]] std::expected<void, Error> Present();

		nvrhi::ITexture* GetCurrentTexture() const { return m_Textures[m_ImageIndex]; }
		nvrhi::IFramebuffer* GetCurrentFramebuffer() const { return m_Framebuffers[m_ImageIndex]; }
		nvrhi::FramebufferInfo GetFramebufferInfo() const;
		nvrhi::Format GetFormat() const { return m_Format; }
		glm::uvec2 GetSize() const { return m_Size; }

	private:
		[[nodiscard]] std::expected<void, Error> CreateSurface();
		[[nodiscard]] std::expected<void, Error> CreateSwapchain();
		void DestroySwapchainResources();
		[[nodiscard]] std::expected<void, Error> CreateSemaphores();
		void LimitFramesInFlight();

	private:
		GraphicsDevice* m_Device;
		const Window* m_Window;
		SwapchainConfig m_Config;

		VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
		VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
		// Chosen with the surface, so it's known before the first images exist
		VkFormat m_VulkanFormat = VK_FORMAT_UNDEFINED;
		nvrhi::Format m_Format = nvrhi::Format::UNKNOWN;
		glm::uvec2 m_Size{0};
		bool m_NeedsRebuild = false;

		std::vector<nvrhi::TextureHandle> m_Textures;
		std::vector<nvrhi::FramebufferHandle> m_Framebuffers;
		uint32_t m_ImageIndex = 0;

		// Signaled when an acquired image is ready to render into; used round-robin
		std::vector<VkSemaphore> m_AcquireSemaphores;
		uint32_t m_AcquireSemaphoreIndex = 0;
		// Signaled when an image's rendering is done and it can be presented; one per image
		std::vector<VkSemaphore> m_PresentSemaphores;

		std::deque<nvrhi::EventQueryHandle> m_FramesInFlight;
		std::vector<nvrhi::EventQueryHandle> m_FreeQueries;
	};

}
