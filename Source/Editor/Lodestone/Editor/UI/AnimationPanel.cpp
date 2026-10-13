#include "Lodestone/Editor/UI/AnimationPanel.h"

#include "Lodestone/Animation/Animation.h"
#include "Lodestone/Animation/AnimationComponents.h"
#include "Lodestone/Editor/UI/UIHelpers.h"
#include "Lodestone/Scene/Entity.h"

#include <imgui.h>
#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <string>

namespace Lodestone {

	namespace {

		std::string GetClipLabel(const Model& model, uint32_t clip)
		{
			if (clip >= model.Animations.size())
				return fmt::format("Clip {} (missing)", clip);
			const std::string& name = model.Animations[clip].Name;
			return name.empty() ? fmt::format("Clip {}", clip) : name;
		}

		void Preview(EditorContext& context, UUID entity, uint32_t clip, float time, bool playing)
		{
			if (auto previewed = context.SetAnimationPreview(entity, clip, time, playing); !previewed)
				ReportFailure("Previewing the clip", previewed.error());
		}

		void DrawTimeline(EditorContext& context, UUID animatorId)
		{
			Entity entity = context.GetScene().FindEntity(animatorId);
			const AnimatorComponent& animator = entity.Get<AnimatorComponent>();
			ImGui::TextUnformatted(entity.GetName().c_str());
			const auto* instance = entity.TryGet<ModelInstanceComponent>();
			const ModelLookup& models = context.GetModelLookup();
			const Model* model = instance != nullptr && models ? models(instance->Model) : nullptr;
			if (model == nullptr)
			{
				ImGui::TextDisabled("Its model isn't loaded: it loads when the viewport draws it");
				return;
			}
			if (model->Animations.empty())
			{
				ImGui::TextDisabled("Its model has no clips");
				return;
			}

			// While editing, what's shown is the preview, or the animator's state until there is one
			const bool editing = context.GetPlayState() == PlayState::Editing;
			const auto& preview = context.GetAnimationPreview();
			const bool previewing = editing && preview && preview->Pose.Entity == animatorId;
			const uint32_t clip = previewing ? preview->Pose.Clip : animator.Clip;
			const float time = previewing ? preview->Pose.Time : animator.Time;
			const bool playing = previewing ? preview->Playing : (!editing && animator.Playing);

			ImGui::BeginDisabled(!editing);
			if (ImGui::BeginCombo("Clip", GetClipLabel(*model, clip).c_str()))
			{
				for (uint32_t index = 0; index < model->Animations.size(); ++index)
				{
					ImGui::PushID(static_cast<int>(index));
					if (ImGui::Selectable(GetClipLabel(*model, index).c_str(), index == clip))
						Preview(context, animatorId, index, 0.0f, playing);
					ImGui::PopID();
				}
				ImGui::EndCombo();
			}
			if (clip < model->Animations.size())
			{
				const AnimationClip& shown = model->Animations[clip];
				float shownTime = GetClipTime(shown, time, animator.Loop);
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::SliderFloat("##Time", &shownTime, 0.0f, shown.Duration, "%.2f s"))
					Preview(context, animatorId, clip, shownTime, false);
				if (ImGui::Button(playing ? "Pause" : "Play"))
					Preview(context, animatorId, clip, shownTime, !playing);
				ImGui::SameLine();
				ImGui::BeginDisabled(!previewing);
				if (ImGui::Button("Stop"))
					context.StopAnimationPreview();
				ImGui::EndDisabled();
				ImGui::SameLine();
				ImGui::TextDisabled(
					"%.2f / %.2f s", static_cast<double>(shownTime), static_cast<double>(shown.Duration));
			}
			ImGui::EndDisabled();

			if (editing)
			{
				ImGui::TextDisabled(
					previewing ? "Previewing: the scene isn't changed" : "Pick a clip, play it or scrub to preview it");
			}
			else if (animator.FadeDuration > 0.0f)
			{
				const float weight = GetFadeWeight(animator, animator.FadeElapsed);
				ImGui::TextDisabled("Crossfading from %s: %.0f%%", GetClipLabel(*model, animator.FadeClip).c_str(),
					static_cast<double>(weight * 100.0f));
			}
			else
			{
				ImGui::TextDisabled("The running animator");
			}
		}

	}

	void AnimationPanel::Draw(EditorContext& context, bool* open)
	{
		if (!ImGui::Begin(Title, open))
		{
			ImGui::End();
			return;
		}
		const UUID animator = FindAnimator(context.GetScene(), context.GetSelection());
		if (animator.IsNil())
			ImGui::TextDisabled("Select an entity with an Animator, or a node of its model");
		else
			DrawTimeline(context, animator);
		ImGui::End();
	}

}
