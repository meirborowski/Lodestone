#pragma once

#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/Error.h"
#include "Lodestone/Core/Image.h"
#include "Lodestone/Core/UUID.h"
#include "Lodestone/Editor/EditorContext.h"
#include "Lodestone/Graphics/DebugSceneRenderer.h"
#include "Lodestone/Graphics/GraphicsDevice.h"
#include "Lodestone/Graphics/RenderTarget.h"

#include <glm/vec2.hpp>
#include <nvrhi/nvrhi.h>

#include <expected>
#include <vector>

namespace Lodestone {

	// Renders the open document from the editor camera: into the viewport's render target each frame, and into a
	// separate one for screenshots, so a screenshot's size never disturbs the viewport. Until the scene renderer
	// arrives (Milestone 5), each entity is a cube on a ground grid
	class SceneView
	{
	private:
		struct Passkey
		{
			explicit Passkey() = default;
		};

	public:
		[[nodiscard]] static std::expected<Scope<SceneView>, Error> Create(GraphicsDevice& device);

		SceneView(Passkey passkey, GraphicsDevice& device);

		// Sizes the viewport's render target and returns its color texture, which RenderViewport() draws into. The
		// editor UI shows the texture as it's built, and renders the scene once the frame's edits are made
		[[nodiscard]] std::expected<nvrhi::ITexture*, Error> PrepareViewport(glm::uvec2 size);
		// Renders into the viewport's render target, if PrepareViewport() created it
		void RenderViewport(const EditorContext& context);
		// Renders an image of a size and reads it back
		[[nodiscard]] std::expected<Image, Error> Capture(const EditorContext& context, glm::uvec2 size);

		// What the view draws: a cube for each entity, the selection highlighted
		static std::vector<DebugCube> BuildCubes(const EditorContext& context);
		// The camera's view and projection for an image of a size
		static glm::mat4 GetViewProjection(const EditorContext& context, glm::uvec2 size);
		// The entity whose cube is nearest the camera under a point of an image of a size (in pixels from its top
		// left corner), or the nil UUID if there's none
		static UUID PickEntity(const EditorContext& context, glm::uvec2 size, glm::vec2 point);

	private:
		[[nodiscard]] static std::expected<void, Error> Fit(
			nvrhi::IDevice* device, Scope<RenderTarget>& target, glm::uvec2 size, const char* debugName);
		void Render(const EditorContext& context, const RenderTarget& target);

	private:
		GraphicsDevice* m_Device;
		Scope<DebugSceneRenderer> m_Renderer;
		Scope<RenderTarget> m_Viewport;
		Scope<RenderTarget> m_Capture;
		nvrhi::CommandListHandle m_CommandList;
	};

}
