#include "Lodestone/Editor/UI/ViewportPanel.h"

#include "Lodestone/Editor/Commands/EntityCommands.h"
#include "Lodestone/Editor/SceneView.h"
#include "Lodestone/Editor/UI/UIHelpers.h"
#include "Lodestone/Scene/Entity.h"
#include "Lodestone/Serialization/Json.h"

#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/matrix.hpp>
// ImGuizmo.h uses Dear ImGui's types without including it
#include <imgui.h>
#include <ImGuizmo.h>
#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <cmath>

namespace Lodestone {

	namespace {

		// Radians of orbit per pixel dragged
		constexpr float OrbitSpeed = 0.005f;
		// How much one wheel notch moves the camera toward its target
		constexpr float ZoomStep = 0.85f;
		constexpr float TranslateSnap = 0.5f;
		constexpr float RotateSnap = 15.0f;
		constexpr float ScaleSnap = 0.1f;
		// Weight of each new frame in the frame time the stats show
		constexpr float FrameTimeSmoothing = 0.05f;
		constexpr float MinScale = 1e-6f;

		ImGuizmo::OPERATION ToImGuizmo(GizmoOperation operation)
		{
			switch (operation)
			{
				case GizmoOperation::Translate:
					return ImGuizmo::TRANSLATE;
				case GizmoOperation::Rotate:
					return ImGuizmo::ROTATE;
				case GizmoOperation::Scale:
					return ImGuizmo::SCALE;
			}
			return ImGuizmo::TRANSLATE;
		}

		bool IsEditing(const EditorContext& context)
		{
			return context.GetPlayState() == PlayState::Editing;
		}

		float GetSnap(GizmoOperation operation)
		{
			switch (operation)
			{
				case GizmoOperation::Translate:
					return TranslateSnap;
				case GizmoOperation::Rotate:
					return RotateSnap;
				case GizmoOperation::Scale:
					return ScaleSnap;
			}
			return TranslateSnap;
		}

	}

	void ViewportPanel::Draw(EditorContext& context, const PrepareImage& prepareImage, float frameSeconds, bool* open)
	{
		m_Visible = false;
		m_Focused = false;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		const bool shown = ImGui::Begin(Title, open);
		ImGui::PopStyleVar();
		if (!shown)
		{
			ImGui::End();
			return;
		}

		DrawToolbar();
		const ImVec2 available = ImGui::GetContentRegionAvail();
		if (available.x < 1.0f || available.y < 1.0f)
		{
			ImGui::End();
			return;
		}
		const glm::vec2 size(std::floor(available.x), std::floor(available.y));
		const auto texture = prepareImage(glm::uvec2(size));
		if (!texture)
		{
			ImGui::TextWrapped("The viewport can't render: %s", texture.error().ToString().c_str());
			ImGui::End();
			return;
		}

		const ImVec2 cursor = ImGui::GetCursorScreenPos();
		const glm::vec2 position(cursor.x, cursor.y);
		ImGui::Image(ImTextureRef(*texture), ImVec2(size.x, size.y));
		m_Visible = true;
		const bool hovered = ImGui::IsItemHovered();
		m_Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		if (IsEditing(context))
		{
			if (const auto asset = AcceptAssetDrop())
			{
				if (const auto instance = context.InstantiatePrefab(*asset); instance)
					context.Select(*instance);
				else
					ReportFailure("Instancing " + *asset, instance.error());
			}
		}

		if (m_Focused)
			HandleShortcuts(context);
		DrawGizmo(context, position, size);
		if (hovered)
		{
			HandleCamera(context, size);
			// Clicking anything but the gizmo selects what's under the cursor
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !m_UsingGizmo)
			{
				const ImVec2 mouse = ImGui::GetMousePos();
				context.Select(
					SceneView::PickEntity(context, glm::uvec2(size), glm::vec2(mouse.x, mouse.y) - position));
			}
		}
		if (m_ShowStats)
			DrawStats(context, position, frameSeconds);
		ImGui::End();
	}

	void ViewportPanel::FrameSelection(EditorContext& context)
	{
		const Entity entity = context.GetScene().FindEntity(context.GetSelection());
		if (!entity)
			return;
		EditorCamera& camera = context.GetCamera();
		const glm::vec3 target(context.GetScene().GetWorldMatrix(entity)[3]);
		camera.LookAt(target + camera.GetPosition() - camera.GetTarget(), target);
	}

	void ViewportPanel::DrawToolbar()
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
		ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 4.0f, ImGui::GetCursorPosY() + 4.0f));
		const auto operationButton = [this](const char* label, GizmoOperation operation, const char* tooltip)
		{
			if (ImGui::RadioButton(label, m_Operation == operation))
				m_Operation = operation;
			ImGui::SetItemTooltip("%s", tooltip);
			ImGui::SameLine();
		};
		operationButton("Move", GizmoOperation::Translate, "Move the selection (W)");
		operationButton("Rotate", GizmoOperation::Rotate, "Rotate the selection (E)");
		operationButton("Scale", GizmoOperation::Scale, "Scale the selection (R)");
		ImGui::TextDisabled("|");
		ImGui::SameLine();
		if (ImGui::Button(m_LocalSpace ? "Local" : "World"))
			m_LocalSpace = !m_LocalSpace;
		ImGui::SetItemTooltip("The gizmo's axes: the entity's own, or the world's");
		ImGui::SameLine();
		ImGui::Checkbox("Snap", &m_Snap);
		ImGui::SetItemTooltip(
			"Move by %.1f m, rotate by %.0f degrees and scale by %.1f", TranslateSnap, RotateSnap, ScaleSnap);
		ImGui::SameLine();
		ImGui::Checkbox("Stats", &m_ShowStats);
		ImGui::PopStyleVar();
		ImGui::Spacing();
	}

	void ViewportPanel::HandleShortcuts(EditorContext& context)
	{
		const ImGuiIO& io = ImGui::GetIO();
		// The right mouse button orbits, and play mode takes the keyboard
		if (io.WantTextInput || io.KeyCtrl || ImGui::IsMouseDown(ImGuiMouseButton_Right) || !IsEditing(context))
			return;
		if (ImGui::IsKeyPressed(ImGuiKey_W, false))
			m_Operation = GizmoOperation::Translate;
		if (ImGui::IsKeyPressed(ImGuiKey_E, false))
			m_Operation = GizmoOperation::Rotate;
		if (ImGui::IsKeyPressed(ImGuiKey_R, false))
			m_Operation = GizmoOperation::Scale;
		if (ImGui::IsKeyPressed(ImGuiKey_F, false))
			FrameSelection(context);
	}

	void ViewportPanel::HandleCamera(EditorContext& context, glm::vec2 size) const
	{
		if (m_UsingGizmo)
			return;
		const ImGuiIO& io = ImGui::GetIO();
		EditorCamera& camera = context.GetCamera();
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f))
			camera.Orbit(-io.MouseDelta.x * OrbitSpeed, io.MouseDelta.y * OrbitSpeed);
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))
		{
			// World units per pixel at the target, so the point under the cursor follows it
			const float scale =
				2.0f * camera.GetDistance() * std::tan(camera.GetFieldOfView() * 0.5f) / std::max(size.y, 1.0f);
			camera.Pan(-io.MouseDelta.x * scale, io.MouseDelta.y * scale);
		}
		if (io.MouseWheel != 0.0f)
			camera.Zoom(std::pow(ZoomStep, io.MouseWheel));
	}

	void ViewportPanel::DrawGizmo(EditorContext& context, glm::vec2 position, glm::vec2 size)
	{
		const UUID selection = context.GetSelection();
		const Entity entity = context.GetScene().FindEntity(selection);
		if (!entity || !IsEditing(context))
		{
			if (m_UsingGizmo)
				context.GetHistory().EndMerge();
			m_UsingGizmo = false;
			return;
		}

		const EditorCamera& camera = context.GetCamera();
		const glm::mat4 view = camera.GetView();
		const glm::mat4 projection = camera.GetProjection(size.x / size.y);
		glm::mat4 world = context.GetScene().GetWorldMatrix(entity);

		ImGuizmo::SetDrawlist();
		ImGuizmo::SetRect(position.x, position.y, size.x, size.y);
		const glm::vec3 snap(GetSnap(m_Operation));
		const bool changed = ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection),
			ToImGuizmo(m_Operation), m_LocalSpace ? ImGuizmo::LOCAL : ImGuizmo::WORLD, glm::value_ptr(world), nullptr,
			m_Snap ? glm::value_ptr(snap) : nullptr);

		const bool active = ImGuizmo::IsUsing();
		if (changed && active)
		{
			// Back to the entity's own space, then apart into position, rotation and scale
			const UUID parent = entity.Get<HierarchyComponent>().Parent;
			const Entity parentEntity = context.GetScene().FindEntity(parent);
			const glm::mat4 local =
				parentEntity ? glm::inverse(context.GetScene().GetWorldMatrix(parentEntity)) * world : world;
			glm::vec3 scale(
				glm::length(glm::vec3(local[0])), glm::length(glm::vec3(local[1])), glm::length(glm::vec3(local[2])));
			if (scale.x > MinScale && scale.y > MinScale && scale.z > MinScale)
			{
				if (glm::determinant(glm::mat3(local)) < 0.0f)
					scale.x = -scale.x;
				const glm::mat3 rotation(
					glm::vec3(local[0]) / scale.x, glm::vec3(local[1]) / scale.y, glm::vec3(local[2]) / scale.z);
				Json::Value fields = Json::Value::object();
				fields["Position"] = Json::FromFieldValue(glm::vec3(local[3]));
				fields["Rotation"] = Json::FromFieldValue(glm::normalize(glm::quat_cast(rotation)));
				fields["Scale"] = Json::FromFieldValue(scale);
				RunCommand(context, CreateScope<SetFieldsCommand>(selection, "Transform", std::move(fields), true));
			}
		}
		if (m_UsingGizmo && !active)
			context.GetHistory().EndMerge();
		m_UsingGizmo = active;
	}

	void ViewportPanel::DrawStats(const EditorContext& context, glm::vec2 position, float frameSeconds)
	{
		m_SmoothedFrameSeconds = m_SmoothedFrameSeconds == 0.0f
			? frameSeconds
			: m_SmoothedFrameSeconds + (frameSeconds - m_SmoothedFrameSeconds) * FrameTimeSmoothing;
		const float fps = m_SmoothedFrameSeconds > 0.0f ? 1.0f / m_SmoothedFrameSeconds : 0.0f;
		const size_t entities = context.GetScene().GetEntityCount();
		// The grid, then a cube per entity
		const std::string text = fmt::format("{:.2f} ms ({:.0f} FPS)\n{} entities\n{} draw calls\n{}",
			m_SmoothedFrameSeconds * 1000.0f, fps, entities, entities + 1, ToString(context.GetPlayState()));

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImVec2 padding(8.0f, 6.0f);
		const ImVec2 corner(position.x + 8.0f, position.y + 8.0f);
		const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
		drawList->AddRectFilled(corner,
			ImVec2(corner.x + textSize.x + padding.x * 2.0f, corner.y + textSize.y + padding.y * 2.0f),
			IM_COL32(0, 0, 0, 140), 4.0f);
		drawList->AddText(
			ImVec2(corner.x + padding.x, corner.y + padding.y), IM_COL32(230, 230, 230, 255), text.c_str());
	}

}
