#include "Lodestone/Animation/Animation.h"

#include "Lodestone/Core/Assert.h"
#include "Lodestone/Scene/Scene.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace Lodestone {

	namespace {

		glm::quat ToQuaternion(const glm::vec4& value)
		{
			return glm::quat::wxyz(value.w, value.x, value.y, value.z);
		}

		glm::vec4 FromQuaternion(const glm::quat& rotation)
		{
			return {rotation.x, rotation.y, rotation.z, rotation.w};
		}

		// What a channel holds when it has no keyframes: no movement
		glm::vec4 GetNeutralValue(AnimationPath path)
		{
			switch (path)
			{
				case AnimationPath::Rotation:
					return {0.0f, 0.0f, 0.0f, 1.0f};
				case AnimationPath::Scale:
					return {1.0f, 1.0f, 1.0f, 0.0f};
				case AnimationPath::Translation:
					break;
			}
			return glm::vec4(0.0f);
		}

		glm::vec4 NormalizeRotation(const glm::vec4& value)
		{
			const float length = glm::length(value);
			return length > 1e-12f && std::isfinite(length) ? value / length : glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		}

		const AnimationClip* FindClip(const Model* model, uint32_t clip)
		{
			return model != nullptr && clip < model->Animations.size() ? &model->Animations[clip] : nullptr;
		}

	}

	float GetClipTime(const AnimationClip& clip, float time, bool loop)
	{
		if (!std::isfinite(time) || !(clip.Duration > 0.0f))
			return 0.0f;
		if (!loop)
			return std::clamp(time, 0.0f, clip.Duration);
		// Within the clip - its end included, which shows its last keyframe - time stands; beyond, it wraps
		if (time >= 0.0f && time <= clip.Duration)
			return time;
		const float wrapped = std::fmod(time, clip.Duration);
		return wrapped < 0.0f ? wrapped + clip.Duration : wrapped;
	}

	glm::vec4 SampleChannel(const AnimationChannel& channel, float time)
	{
		const std::vector<float>& times = channel.Times;
		const bool spline = channel.Interpolation == AnimationInterpolation::CubicSpline;
		const size_t valuesPerKey = spline ? 3 : 1;
		if (times.empty() || channel.Values.size() < times.size() * valuesPerKey)
			return GetNeutralValue(channel.Path);
		const auto value = [&channel, spline](size_t key) { return channel.Values[spline ? key * 3 + 1 : key]; };

		// Before the first keyframe and after the last, their values hold. Written so a NaN time is the first
		if (!(time > times.front()))
			return value(0);
		if (!(time < times.back()))
			return value(times.size() - 1);

		// The keyframes either side: the last one at or before the time, and the next
		const auto next = std::ranges::upper_bound(times, time);
		const auto key = static_cast<size_t>(next - times.begin()) - 1;
		const float span = times[key + 1] - times[key];
		if (channel.Interpolation == AnimationInterpolation::Step || !(span > 0.0f))
			return value(key);
		const float factor = (time - times[key]) / span;
		const bool rotation = channel.Path == AnimationPath::Rotation;

		if (!spline)
		{
			if (rotation)
				return FromQuaternion(glm::slerp(ToQuaternion(value(key)), ToQuaternion(value(key + 1)), factor));
			return glm::mix(value(key), value(key + 1), factor);
		}

		// glTF's cubic Hermite spline: each keyframe's out-tangent and the next one's in-tangent, scaled by the time
		// between them
		const float t2 = factor * factor;
		const float t3 = t2 * factor;
		const glm::vec4 start = value(key);
		const glm::vec4 startTangent = channel.Values[key * 3 + 2] * span;
		const glm::vec4 end = value(key + 1);
		const glm::vec4 endTangent = channel.Values[(key + 1) * 3] * span;
		const glm::vec4 result = (2.0f * t3 - 3.0f * t2 + 1.0f) * start + (t3 - 2.0f * t2 + factor) * startTangent +
			(-2.0f * t3 + 3.0f * t2) * end + (t3 - t2) * endTangent;
		return rotation ? NormalizeRotation(result) : result;
	}

	void ApplyClip(const AnimationClip& clip, float time, std::span<TransformComponent> pose)
	{
		for (const AnimationChannel& channel : clip.Channels)
		{
			if (channel.Node >= pose.size())
				continue;
			TransformComponent& transform = pose[channel.Node];
			const glm::vec4 value = SampleChannel(channel, time);
			switch (channel.Path)
			{
				case AnimationPath::Translation:
					transform.Position = glm::vec3(value);
					break;
				case AnimationPath::Rotation:
					transform.Rotation = ToQuaternion(value);
					break;
				case AnimationPath::Scale:
					transform.Scale = glm::vec3(value);
					break;
			}
		}
	}

	void BlendPose(std::span<const TransformComponent> from, std::span<TransformComponent> to, float weight)
	{
		LS_CORE_ASSERT(from.size() == to.size(), "Blending poses of {} and {} nodes", from.size(), to.size());
		for (size_t node = 0; node < to.size(); ++node)
			to[node] = InterpolateTransform(from[node], to[node], weight);
	}

	float GetFadeWeight(const AnimatorComponent& animator, float elapsed)
	{
		if (!(animator.FadeDuration > 0.0f))
			return 1.0f;
		return std::clamp(elapsed / animator.FadeDuration, 0.0f, 1.0f);
	}

	void Play(AnimatorComponent& animator, uint32_t clip)
	{
		animator.Clip = clip;
		animator.Time = 0.0f;
		animator.Playing = true;
		animator.FadeClip = 0;
		animator.FadeTime = 0.0f;
		animator.FadeDuration = 0.0f;
		animator.FadeElapsed = 0.0f;
	}

	void CrossFade(AnimatorComponent& animator, uint32_t clip, float duration)
	{
		if (!(duration > 0.0f))
		{
			Play(animator, clip);
			return;
		}
		animator.FadeClip = animator.Clip;
		animator.FadeTime = animator.Time;
		animator.FadeDuration = duration;
		animator.FadeElapsed = 0.0f;
		animator.Clip = clip;
		animator.Time = 0.0f;
		animator.Playing = true;
	}

	void AdvanceAnimator(AnimatorComponent& animator, float deltaTime, const Model* model)
	{
		if (!animator.Playing)
			return;
		const float step = deltaTime * animator.Speed;
		animator.Time += step;
		if (animator.FadeDuration > 0.0f)
		{
			animator.FadeElapsed += deltaTime;
			if (animator.FadeElapsed >= animator.FadeDuration)
			{
				animator.FadeClip = 0;
				animator.FadeTime = 0.0f;
				animator.FadeDuration = 0.0f;
				animator.FadeElapsed = 0.0f;
			}
			else
			{
				animator.FadeTime += step;
				if (const AnimationClip* fading = FindClip(model, animator.FadeClip))
					animator.FadeTime = GetClipTime(*fading, animator.FadeTime, true);
			}
		}

		const AnimationClip* clip = FindClip(model, animator.Clip);
		if (clip == nullptr)
			return;
		if (animator.Loop)
		{
			animator.Time = GetClipTime(*clip, animator.Time, true);
		}
		else if ((step > 0.0f && animator.Time >= clip->Duration) || (step < 0.0f && animator.Time <= 0.0f))
		{
			animator.Time = GetClipTime(*clip, animator.Time, false);
			animator.Playing = false;
		}
	}

	void AdvanceAnimators(Scene& scene, float deltaTime, const ModelLookup& models)
	{
		entt::registry& registry = scene.GetRegistry();
		for (const auto [handle, animator] : registry.view<AnimatorComponent>().each())
		{
			const auto* instance = registry.try_get<ModelInstanceComponent>(handle);
			const Model* model = instance != nullptr && models ? models(instance->Model) : nullptr;
			AdvanceAnimator(animator, deltaTime, model);
		}
	}

	UUID FindAnimator(const Scene& scene, UUID entity)
	{
		const entt::registry& registry = scene.GetRegistry();
		for (UUID current = entity; !current.IsNil();)
		{
			const entt::entity handle = scene.FindHandle(current);
			if (handle == entt::null)
				return {};
			if (registry.all_of<AnimatorComponent>(handle))
				return current;
			current = registry.get<HierarchyComponent>(handle).Parent;
		}
		return {};
	}

}
