#pragma once

#include "Lodestone/Core/Error.h"
#include "Lodestone/Core/Image.h"
#include "Lodestone/Editor/EditorContext.h"
#include "Lodestone/Editor/Mcp/McpServer.h"

#include <cstdint>
#include <expected>
#include <functional>

namespace Lodestone {

	// Renders the open document from the editor camera, for screenshots. Fails where nothing can render - headless
	// without a graphics device
	using ViewportCapture = std::function<std::expected<Image, Error>(uint32_t width, uint32_t height)>;

	// What the tools need from the application around them
	struct EditorToolHost
	{
		// Empty where nothing can render
		ViewportCapture Capture;
		// Closes the editor once the current request is answered. Empty where the client stops it another way: the
		// stdio editor stops when its standard input closes
		std::function<void()> RequestQuit;
	};

	// Registers the editor's MCP tools: projects and documents, entities and components (driven by the reflection
	// registry), prefabs, undo and redo, play mode, input, screenshots, the camera, logs, assets and quitting. Every
	// change goes through the editor's commands, so it can be undone (see docs/AIControl.md#tools)
	void RegisterEditorTools(McpServer& server, EditorContext& context, EditorToolHost host);

}
