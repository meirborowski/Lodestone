#include "Lodestone/Platform/Window.h"

#include "Lodestone/Core/Assert.h"
#include "Lodestone/Core/Log.h"
#include "Lodestone/Graphics/VulkanLibrary.h"

// GLFW declares its Vulkan functions only when the Vulkan header comes first
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <limits>
#include <utility>

namespace Lodestone {

	namespace {

		// Windows that are open. GLFW is initialized while there's at least one. Main thread only
		int s_WindowCount = 0;

		void OnGlfwError(int code, const char* description)
		{
			LS_CORE_ERROR("GLFW error {:#x}: {}", code, description);
		}

		Error MakeGlfwError(std::string_view operation)
		{
			const char* description = nullptr;
			const int code = glfwGetError(&description);
			return Error(ErrorCode::DeviceError,
				fmt::format("{} failed (GLFW error {:#x}): {}", operation, code,
					description != nullptr ? description : "no description"));
		}

		std::expected<void, Error> AcquireGlfw()
		{
			if (s_WindowCount == 0)
			{
				// GLFW must use the same Vulkan loader as the renderer
				if (const auto getInstanceProcAddr = LoadVulkanLibrary())
					glfwInitVulkanLoader(*getInstanceProcAddr);
				else
					LS_CORE_WARN("Windows can't present with Vulkan: {}", getInstanceProcAddr.error());

				glfwSetErrorCallback(&OnGlfwError);
				if (glfwInit() != GLFW_TRUE)
					return std::unexpected(MakeGlfwError("Initializing GLFW"));
			}
			++s_WindowCount;
			return {};
		}

		void ReleaseGlfw()
		{
			LS_CORE_ASSERT(s_WindowCount > 0);
			if (--s_WindowCount == 0)
				glfwTerminate();
		}

		// The input of the window a GLFW callback is for
		DeviceInput& InputOf(GLFWwindow* handle)
		{
			return static_cast<Window*>(glfwGetWindowUserPointer(handle))->GetInput();
		}

	}

	std::expected<Scope<Window>, Error> Window::Create(const WindowConfig& config)
	{
		constexpr auto maxSize = static_cast<uint32_t>(std::numeric_limits<int>::max());
		if (config.Width == 0 || config.Height == 0 || config.Width > maxSize || config.Height > maxSize)
			return std::unexpected(Error(
				ErrorCode::InvalidArgument, fmt::format("Invalid window size {}x{}", config.Width, config.Height)));

		if (auto acquired = AcquireGlfw(); !acquired)
			return std::unexpected(std::move(acquired).error());

		glfwDefaultWindowHints();
		// Vulkan renders into the window, so GLFW mustn't create an OpenGL context
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, config.Resizable ? GLFW_TRUE : GLFW_FALSE);

		GLFWwindow* handle = glfwCreateWindow(
			static_cast<int>(config.Width), static_cast<int>(config.Height), config.Title.c_str(), nullptr, nullptr);
		if (handle == nullptr)
		{
			Error error = MakeGlfwError("Creating a window");
			ReleaseGlfw();
			return std::unexpected(std::move(error));
		}

		return CreateScope<Window>(Passkey(), handle);
	}

	Window::Window(Passkey /*passkey*/, GLFWwindow* handle)
		: m_Handle(handle)
	{
		glfwSetWindowUserPointer(m_Handle, this);

		glfwSetKeyCallback(m_Handle,
			[](GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
			{
				// Repeats don't change whether the key is down
				if (action == GLFW_REPEAT)
					return;
				if (const std::optional<Key> code = KeyFromCode(key))
					InputOf(window).OnKey(*code, action == GLFW_PRESS);
			});

		glfwSetMouseButtonCallback(m_Handle,
			[](GLFWwindow* window, int button, int action, int /*mods*/)
			{
				if (button >= 0 && std::cmp_less(button, MouseButtonCount))
					InputOf(window).OnMouseButton(static_cast<MouseButton>(button), action == GLFW_PRESS);
			});

		glfwSetCursorPosCallback(m_Handle, [](GLFWwindow* window, double x, double y)
			{ InputOf(window).OnCursorMoved(glm::vec2(static_cast<float>(x), static_cast<float>(y))); });

		glfwSetScrollCallback(m_Handle, [](GLFWwindow* window, double x, double y)
			{ InputOf(window).OnScroll(glm::vec2(static_cast<float>(x), static_cast<float>(y))); });

		glfwSetCharCallback(m_Handle, [](GLFWwindow* window, unsigned int codepoint)
			{ InputOf(window).OnText(static_cast<char32_t>(codepoint)); });

		// A window that loses focus stops getting key-up events, so nothing may stay held down
		glfwSetWindowFocusCallback(m_Handle,
			[](GLFWwindow* window, int focused)
			{
				if (focused == GLFW_FALSE)
					InputOf(window).ReleaseAll();
			});
	}

	Window::~Window()
	{
		glfwDestroyWindow(m_Handle);
		ReleaseGlfw();
	}

	void Window::PollEvents()
	{
		m_Input.BeginFrame();
		glfwPollEvents();
		PollGamepads();
	}

	bool Window::ShouldClose() const
	{
		return glfwWindowShouldClose(m_Handle) == GLFW_TRUE;
	}

	void Window::RequestClose()
	{
		glfwSetWindowShouldClose(m_Handle, GLFW_TRUE);
	}

	void Window::SetTitle(std::string_view title)
	{
		const std::string terminated(title);
		glfwSetWindowTitle(m_Handle, terminated.c_str());
	}

	glm::uvec2 Window::GetFramebufferSize() const
	{
		int width = 0;
		int height = 0;
		glfwGetFramebufferSize(m_Handle, &width, &height);
		return {static_cast<uint32_t>(std::max(width, 0)), static_cast<uint32_t>(std::max(height, 0))};
	}

	bool Window::IsMinimized() const
	{
		const glm::uvec2 size = GetFramebufferSize();
		return size.x == 0 || size.y == 0;
	}

	std::expected<std::vector<std::string>, Error> Window::GetRequiredVulkanInstanceExtensions() const
	{
		uint32_t count = 0;
		const char** names = glfwGetRequiredInstanceExtensions(&count);
		if (names == nullptr)
		{
			// GLFW reports no error when the Vulkan loader works but lists no window surface extensions, which it
			// only does for installed drivers that support them
			if (glfwGetError(nullptr) == GLFW_NO_ERROR)
				return std::unexpected(Error(ErrorCode::DeviceError,
					"No installed Vulkan driver can present to windows (the Vulkan loader lists no window surface "
					"extensions)"));
			return std::unexpected(MakeGlfwError("Finding the Vulkan extensions the window system needs"));
		}
		return std::vector<std::string>(names, names + count);
	}

	bool Window::SupportsVulkanPresentation(
		VkInstance instance, VkPhysicalDevice physicalDevice, uint32_t queueFamily) const
	{
		return glfwGetPhysicalDevicePresentationSupport(instance, physicalDevice, queueFamily) == GLFW_TRUE;
	}

	std::expected<VkSurfaceKHR, Error> Window::CreateVulkanSurface(VkInstance instance) const
	{
		VkSurfaceKHR surface = VK_NULL_HANDLE;
		if (const VkResult result = glfwCreateWindowSurface(instance, m_Handle, nullptr, &surface);
			result != VK_SUCCESS)
			return std::unexpected(Error(ErrorCode::DeviceError,
				fmt::format(
					"Creating a Vulkan surface for the window failed with VkResult {}", static_cast<int>(result))));
		return surface;
	}

	void Window::PollGamepads()
	{
		for (uint32_t index = 0; index < MaxGamepads; ++index)
		{
			const int joystick = GLFW_JOYSTICK_1 + static_cast<int>(index);
			GamepadState state;
			GLFWgamepadstate glfwState;
			if (glfwJoystickIsGamepad(joystick) == GLFW_TRUE && glfwGetGamepadState(joystick, &glfwState) == GLFW_TRUE)
			{
				state.Connected = true;
				if (const char* name = glfwGetGamepadName(joystick))
					state.Name = name;
				for (uint32_t button = 0; button < GamepadButtonCount; ++button)
					state.Buttons[button] = glfwState.buttons[button] == GLFW_PRESS;
				for (uint32_t axis = 0; axis < GamepadAxisCount; ++axis)
					state.Axes[axis] = glfwState.axes[axis];
			}
			m_Input.SetGamepadState(index, state);
		}
	}

}
