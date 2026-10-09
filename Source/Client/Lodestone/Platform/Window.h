#pragma once

#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/Error.h"
#include "Lodestone/Input/DeviceInput.h"

#include <glm/vec2.hpp>
#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

struct GLFWwindow;

namespace Lodestone {

	struct WindowConfig
	{
		std::string Title = "Lodestone";
		// Size of the window's client area, in screen coordinates
		uint32_t Width = 1280;
		uint32_t Height = 720;
		bool Resizable = true;
	};

	// A desktop window that Vulkan renders into, and the source of keyboard, mouse and gamepad input.
	// Windows must be created, used and destroyed on the main thread
	class Window
	{
	private:
		// Restricts construction to Create(), while still allowing CreateScope()
		struct Passkey
		{
			explicit Passkey() = default;
		};

	public:
		[[nodiscard]] static std::expected<Scope<Window>, Error> Create(const WindowConfig& config);

		Window(Passkey passkey, GLFWwindow* handle);
		~Window();

		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;
		Window(Window&&) = delete;
		Window& operator=(Window&&) = delete;

		// Starts a new input frame, then processes pending window events and polls the gamepads. Call once per frame
		void PollEvents();

		bool ShouldClose() const;
		void RequestClose();
		// Keeps the window open after the user asked to close it, e.g. to ask about unsaved changes first
		void CancelClose();
		void SetTitle(std::string_view title);

		// Size of the drawable area, in pixels. Zero while the window is minimized
		glm::uvec2 GetFramebufferSize() const;
		bool IsMinimized() const;

		DeviceInput& GetInput() { return m_Input; }
		const DeviceInput& GetInput() const { return m_Input; }

		// What a Vulkan instance and device need to present to this window. Fails when the window system can't present
		// with Vulkan, e.g. when no Vulkan driver is installed
		[[nodiscard]] std::expected<std::vector<std::string>, Error> GetRequiredVulkanInstanceExtensions() const;
		bool SupportsVulkanPresentation(
			VkInstance instance, VkPhysicalDevice physicalDevice, uint32_t queueFamily) const;
		[[nodiscard]] std::expected<VkSurfaceKHR, Error> CreateVulkanSurface(VkInstance instance) const;

		GLFWwindow* GetNativeHandle() const { return m_Handle; }

	private:
		void PollGamepads();

	private:
		GLFWwindow* m_Handle = nullptr;
		DeviceInput m_Input;
	};

}
