#include "Lodestone/Core/CommandLine.h"
#include "Lodestone/Core/Log.h"
#include "Lodestone/Core/RunMain.h"
#include "Lodestone/Core/Version.h"
#include "Lodestone/Graphics/GraphicsDevice.h"
#include "Lodestone/Graphics/Swapchain.h"
#include "Lodestone/Graphics/TriangleRenderer.h"
#include "Lodestone/Platform/Window.h"

#include <spdlog/fmt/fmt.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <thread>

namespace {

	constexpr std::array<std::string_view, 2> Flags = {"--version", "--no-vsync"};
	// --frames N: exit after rendering N frames, for automated runs
	constexpr std::array<std::string_view, 1> Options = {"--frames"};

	// Until exported games can be loaded (Milestone 12), the runtime opens a window and draws the bring-up triangle
	std::expected<int, Lodestone::Error> Run(const Lodestone::CommandLine& commandLine)
	{
		using namespace Lodestone;

		if (auto valid = commandLine.Validate(Flags, Options); !valid)
			return std::unexpected(std::move(valid).error());
		if (commandLine.HasFlag("--version"))
		{
			fmt::print("Lodestone Runtime {}\n", VersionString);
			return EXIT_SUCCESS;
		}
		const auto frameLimit = commandLine.GetUnsignedValue("--frames");
		if (!frameLimit)
			return std::unexpected(frameLimit.error());

		auto window = Window::Create({.Title = fmt::format("Lodestone Runtime {}", VersionString)});
		if (!window)
			return std::unexpected(std::move(window).error());

		auto presentationExtensions = (*window)->GetRequiredVulkanInstanceExtensions();
		if (!presentationExtensions)
			return std::unexpected(std::move(presentationExtensions).error());

		GraphicsDeviceConfig deviceConfig;
		deviceConfig.PresentationExtensions = std::move(*presentationExtensions);
		deviceConfig.SupportsPresentation =
			[&window](VkInstance instance, VkPhysicalDevice physicalDevice, uint32_t queueFamily)
		{ return (*window)->SupportsVulkanPresentation(instance, physicalDevice, queueFamily); };
		auto device = GraphicsDevice::Create(deviceConfig);
		if (!device)
			return std::unexpected(std::move(device).error());

		auto swapchain = Swapchain::Create(**device, **window, {.VSync = !commandLine.HasFlag("--no-vsync")});
		if (!swapchain)
			return std::unexpected(std::move(swapchain).error());

		nvrhi::IDevice* nvrhiDevice = (*device)->GetNvrhiDevice();
		auto renderer = TriangleRenderer::Create(nvrhiDevice, (*swapchain)->GetFramebufferInfo());
		if (!renderer)
			return std::unexpected(std::move(renderer).error());
		const nvrhi::CommandListHandle commandList = nvrhiDevice->createCommandList();

		uint32_t frames = 0;
		while (!(*window)->ShouldClose() && (!frameLimit->has_value() || frames < **frameLimit))
		{
			(*window)->PollEvents();
			if ((*window)->GetInput().WasKeyPressed(Key::Escape))
				(*window)->RequestClose();

			const auto began = (*swapchain)->BeginFrame();
			if (!began)
				return std::unexpected(began.error());
			if (!*began)
			{
				// Minimized: nothing to draw, so don't spin
				std::this_thread::sleep_for(std::chrono::milliseconds(16));
				continue;
			}

			commandList->open();
			(*renderer)->Render(commandList, (*swapchain)->GetCurrentFramebuffer());
			commandList->close();
			nvrhiDevice->executeCommandList(commandList);

			if (auto presented = (*swapchain)->Present(); !presented)
				return std::unexpected(std::move(presented).error());
			++frames;
		}

		LS_INFO("Rendered {} frames", frames);
		// Validation errors are bugs, so automated runs must catch them. Validation runs in Debug builds only
		if (const uint32_t errors = (*device)->GetValidationErrorCount(); errors > 0)
		{
			LS_ERROR("The validation layers reported {} errors", errors);
			return EXIT_FAILURE;
		}
		return EXIT_SUCCESS;
	}

}

int main(int argc, char** argv)
{
	return Lodestone::RunMain("Lodestone Runtime", {},
		[argc, argv]
		{
			const auto result = Run(Lodestone::CommandLine(argc, argv));
			if (!result)
			{
				LS_CRITICAL("{}", result.error());
				return EXIT_FAILURE;
			}
			return *result;
		});
}
