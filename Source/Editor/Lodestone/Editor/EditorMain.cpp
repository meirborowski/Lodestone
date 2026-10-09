#include "Lodestone/Core/CommandLine.h"
#include "Lodestone/Core/FileSystem.h"
#include "Lodestone/Core/Log.h"
#include "Lodestone/Core/RunMain.h"
#include "Lodestone/Core/Version.h"
#include "Lodestone/Editor/EditorApp.h"

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <span>

#if defined(LS_CONFIG_DIST)
	#error "The editor isn't built in Dist: exported games must not contain editor code"
#endif

namespace {

	// --headless: run without a window, rendering offscreen
	// --mcp-stdio: with --headless, serve MCP over standard input and output
	// --no-vsync: don't wait for the display between frames
	constexpr std::array<std::string_view, 4> Flags = {"--version", "--headless", "--mcp-stdio", "--no-vsync"};
	// --mcp-port <port>: serve MCP over HTTP on this port (0 for any free port)
	// --project <directory>: open this project
	// --vulkan-driver <library>: render with this Vulkan driver instead of the installed ones, e.g. lavapipe
	// --frames <count>: exit after rendering this many frames, for automated runs
	constexpr std::array<std::string_view, 4> Options = {"--mcp-port", "--project", "--vulkan-driver", "--frames"};

	std::expected<int, Lodestone::Error> Run(const Lodestone::CommandLine& commandLine)
	{
		using namespace Lodestone;

		if (auto valid = commandLine.Validate(Flags, Options); !valid)
			return std::unexpected(std::move(valid).error());
		if (commandLine.HasFlag("--version"))
		{
			fmt::print("Lodestone Editor {}\n", VersionString);
			return EXIT_SUCCESS;
		}

		EditorOptions options;
		options.Headless = commandLine.HasFlag("--headless");
		options.McpStdio = commandLine.HasFlag("--mcp-stdio");
		options.VSync = !commandLine.HasFlag("--no-vsync");
		const auto port = commandLine.GetUnsignedValue("--mcp-port");
		if (!port)
			return std::unexpected(port.error());
		if (*port)
		{
			if (**port > std::numeric_limits<uint16_t>::max())
				return std::unexpected(
					Error(ErrorCode::InvalidArgument, fmt::format("--mcp-port {} isn't a port number", **port)));
			options.McpPort = static_cast<uint16_t>(**port);
		}
		if (const auto project = commandLine.GetValue("--project"))
			options.Project = PathFromUtf8(*project);
		if (const auto driver = commandLine.GetValue("--vulkan-driver"))
			options.VulkanDriver = PathFromUtf8(*driver);
		const auto frames = commandLine.GetUnsignedValue("--frames");
		if (!frames)
			return std::unexpected(frames.error());
		options.FrameLimit = *frames;
		return RunEditor(options);
	}

}

int main(int argc, char** argv)
{
	// Over stdio, the standard output carries MCP messages only, so the log goes to the standard error
	const std::span<char*> arguments(argv, static_cast<size_t>(argc));
	const bool stdio = std::ranges::any_of(arguments,
		[](const char* argument) { return argument != nullptr && std::string_view(argument) == "--mcp-stdio"; });
	const Lodestone::LogConfig logConfig{
		.Console = stdio ? Lodestone::LogConsole::StandardError : Lodestone::LogConsole::StandardOutput};
	return Lodestone::RunMain("Lodestone Editor", logConfig,
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
