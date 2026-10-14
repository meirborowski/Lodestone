#pragma once

#include "Lodestone/Asset/Model.h"
#include "Lodestone/Core/Base.h"
#include "Lodestone/Core/Error.h"
#include "Lodestone/Core/Image.h"
#include "Lodestone/Core/UUID.h"
#include "Lodestone/Editor/EditorContext.h"
#include "Lodestone/Graphics/GraphicsDevice.h"
#include "Lodestone/Renderer/LineRenderer.h"
#include "Lodestone/Renderer/RenderStats.h"
#include "Lodestone/Renderer/RenderWorld.h"
#include "Lodestone/Renderer/SceneRenderer.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Lodestone {

	// What the editor draws over the scene
	struct SceneOverlay
	{
		// Hidden behind the scene: the ground grid, and a marker for each entity that draws nothing of its own
		std::vector<LineVertex> DepthTested;
		// Over everything: the selection's outline
		std::vector<LineVertex> OnTop;
	};

	// Which camera a capture sees the scene from
	enum class CaptureCamera
	{
		// The editor camera, with the editor's overlay: what the viewport shows
		Editor,
		// The scene's primary camera, without the overlay: what the game shows
		Scene,
	};

	// Renders the open document from the editor camera with the scene renderer, with the editor's overlay on top - a
	// ground grid, a marker for each entity that draws nothing of its own (lights, cameras, empty entities), and the
	// selection outlined. It renders into the viewport's target each frame, and into a separate one for screenshots,
	// so a screenshot's size never disturbs the viewport. Models and environments come from the project's assets
	class SceneView
	{
	private:
		struct Passkey
		{
			explicit Passkey() = default;
		};

	public:
		// The width of an entity marker, in meters
		static constexpr float MarkerSize = 0.3f;
		// The grid covers this many meters to each side of the origin, a line a meter
		static constexpr int GridHalfSize = 20;

		[[nodiscard]] static std::expected<Scope<SceneView>, Error> Create(GraphicsDevice& device);

		SceneView(Passkey passkey, GraphicsDevice& device);
		~SceneView();

		SceneView(const SceneView&) = delete;
		SceneView& operator=(const SceneView&) = delete;
		SceneView(SceneView&&) = delete;
		SceneView& operator=(SceneView&&) = delete;

		// Sizes the viewport's render target and returns its color texture, which RenderViewport() draws into. The
		// editor UI shows the texture as it's built, and renders the scene once the frame's edits are made
		[[nodiscard]] std::expected<nvrhi::ITexture*, Error> PrepareViewport(glm::uvec2 size);
		// Renders into the viewport's render target, if PrepareViewport() created it
		void RenderViewport(const EditorContext& context);
		// Renders an image of a size and reads it back. From the scene's camera, it fails if the scene has none
		[[nodiscard]] std::expected<Image, Error> Capture(
			const EditorContext& context, glm::uvec2 size, CaptureCamera camera = CaptureCamera::Editor);

		// The latest frame's stats: the viewport's, or for an editor without one, the latest screenshot's. Empty
		// before anything renders
		std::optional<RenderStats> GetStats() const;
		// The entity under a point of an image of a size (in pixels from its top left corner), or the nil UUID
		UUID PickEntity(const EditorContext& context, glm::uvec2 size, glm::vec2 point) const;
		// The models the renderer has loaded, on the CPU. The lookup reads the view, so it mustn't outlive it
		ModelLookup GetModelLookup() const;

		// Where the editor camera looks from, for the renderer
		static RenderView GetRenderView(const EditorContext& context);
		// The editor camera's view and projection for an image of a size, as the gizmo and picking use them
		static glm::mat4 GetViewProjection(const EditorContext& context, glm::uvec2 size);
		// What the overlay draws for the open document, where the scene is drawn (between ticks, while playing). Meshes
		// whose models aren't loaded get markers
		static SceneOverlay BuildOverlay(const EditorContext& context, const ModelLookup& models);
		// The same, for the scene posed already - as the frame that draws it was
		static SceneOverlay BuildOverlay(const EditorContext& context, const ScenePose& pose);
		// The entity under a point: the nearest mesh it hits, triangle by triangle, or the nearest marker - where the
		// scene is drawn
		static UUID PickEntity(
			const EditorContext& context, glm::uvec2 size, glm::vec2 point, const ModelLookup& models);

	private:
		// Points the renderer at the project's assets, and forgets loaded ones whose files have changed
		void SyncAssets(const EditorContext& context);
		// Notes when an asset's file last changed, as it's loaded
		void RecordWriteTime(const AssetRegistry& registry, UUID id);
		// Whether there was a camera to render from: the scene may have none
		bool Render(const EditorContext& context, SceneRenderTarget& target, CaptureCamera camera);

	private:
		GraphicsDevice* m_Device;
		Scope<SceneRenderer> m_Renderer;
		Scope<LineRenderer> m_Lines;
		Scope<SceneRenderTarget> m_Viewport;
		Scope<SceneRenderTarget> m_Capture;
		bool m_ViewportRendered = false;
		bool m_CaptureRendered = false;
		nvrhi::CommandListHandle m_CommandList;

		// The asset registry the renderer loads from, and when each asset it loaded last changed on disk
		const AssetRegistry* m_AssetSource = nullptr;
		uint64_t m_AssetGeneration = 0;
		std::unordered_map<UUID, std::filesystem::file_time_type> m_AssetWriteTimes;
	};

}
