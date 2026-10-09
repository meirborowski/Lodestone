#pragma once

#include "Lodestone/Core/Error.h"
#include "Lodestone/Editor/EditorContext.h"

#include <glm/vec2.hpp>
#include <imgui.h>

#include <expected>
#include <functional>

namespace Lodestone {

	enum class GizmoOperation
	{
		Translate,
		Rotate,
		Scale,
	};

	// The scene from the editor camera, with the gizmo for the selection. Right drag orbits, middle drag pans, the
	// wheel zooms, F frames the selection, W, E and R pick the gizmo's operation, and clicking selects
	class ViewportPanel
	{
	public:
		static constexpr const char* Title = "Viewport";

		// Sizes the scene's image and returns its texture, to show this frame
		using PrepareImage = std::function<std::expected<ImTextureID, Error>(glm::uvec2 size)>;

		void Draw(EditorContext& context, const PrepareImage& prepareImage, float frameSeconds, bool* open);

		// Whether the panel showed the scene this frame, so it needs rendering
		bool IsVisible() const { return m_Visible; }
		// Whether the panel has keyboard focus, so play mode takes the keyboard and mouse
		bool IsFocused() const { return m_Focused; }

		GizmoOperation GetGizmoOperation() const { return m_Operation; }
		void SetGizmoOperation(GizmoOperation operation) { m_Operation = operation; }

		// Points the camera at the selection, keeping its angle
		static void FrameSelection(EditorContext& context);

	private:
		void DrawToolbar();
		void HandleShortcuts(EditorContext& context);
		void HandleCamera(EditorContext& context, glm::vec2 size) const;
		void DrawGizmo(EditorContext& context, glm::vec2 position, glm::vec2 size);
		void DrawStats(const EditorContext& context, glm::vec2 position, float frameSeconds);

	private:
		GizmoOperation m_Operation = GizmoOperation::Translate;
		bool m_LocalSpace = true;
		bool m_Snap = false;
		bool m_ShowStats = true;
		bool m_UsingGizmo = false;
		bool m_Visible = false;
		bool m_Focused = false;
		float m_SmoothedFrameSeconds = 0.0f;
	};

}
