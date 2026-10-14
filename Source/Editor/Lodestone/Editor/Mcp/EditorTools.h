#pragma once

#include "Lodestone/Core/Error.h"
#include "Lodestone/Core/Image.h"
#include "Lodestone/Editor/EditorContext.h"
#include "Lodestone/Editor/Mcp/McpServer.h"
#include "Lodestone/Editor/SceneView.h"
#include "Lodestone/Renderer/RenderStats.h"

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>

namespace Lodestone {

	// Renders the open document from a camera, for screenshots (see SceneView::Capture()). Fails where nothing can
	// render - headless without a graphics device
	using ViewportCapture =
		std::function<std::expected<Image, Error>(uint32_t width, uint32_t height, CaptureCamera camera)>;

	// What the tools need from the application around them
	struct EditorToolHost
	{
		// Empty where nothing can render
		ViewportCapture Capture;
		// The renderer's stats for its latest frame - the viewport's, or headless, the latest screenshot's. Empty where
		// nothing can render
		std::function<std::optional<RenderStats>()> GetRenderStats;
		// Closes the editor once the current request is answered. Empty where the client stops it another way: the
		// stdio editor stops when its standard input closes
		std::function<void()> RequestQuit;
	};

	// Registers the editor's MCP tools: projects and documents, entities and components (driven by the reflection
	// registry), prefabs and models, undo and redo, play mode, input, screenshots and render stats, the camera, logs,
	// assets and quitting. Every change goes through the editor's commands, so it can be undone (see
	// docs/AIControl.md#tools)
	void RegisterEditorTools(McpServer& server, EditorContext& context, EditorToolHost host);

}
