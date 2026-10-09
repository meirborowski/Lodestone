#pragma once

#include "Lodestone/Core/Error.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>

namespace Lodestone {

	struct EditorOptions
	{
		// The MCP server's HTTP port when none is given. .mcp.json points at it
		static constexpr uint16_t DefaultMcpPort = 7850;

		// Runs without a window, rendering offscreen, until the process is stopped or the stdio client leaves
		bool Headless = false;
		// Serves MCP over standard input and output (headless only), for clients that start the editor themselves
		bool McpStdio = false;
		// The HTTP transport's port; 0 picks a free one. Without a port, the windowed and plain headless editors use
		// DefaultMcpPort, and the stdio editor serves no HTTP, so clients can each start one without clashing
		std::optional<uint16_t> McpPort;
		// A project to open at startup
		std::optional<std::filesystem::path> Project;
		// Render with this Vulkan driver instead of the installed ones, e.g. lavapipe
		std::filesystem::path VulkanDriver;
		// The windowed editor exits after this many frames, for automated runs
		std::optional<uint32_t> FrameLimit;
		bool VSync = true;
	};

	// Runs the editor until it's closed. Returns the exit code
	[[nodiscard]] std::expected<int, Error> RunEditor(const EditorOptions& options);

}
