#include "Lodestone/Animation/Animation.h"

#include "Lodestone/Scene/Entity.h"
#include "Lodestone/Scene/Scene.h"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <cmath>
#include <limits>
#include <vector>

namespace Lodestone {

	namespace {

		bool Near(const glm::vec4& a, const glm::vec4& b, float tolerance = 1e-5f)
		{
			return glm::length(a - b) <= tolerance;
		}

		bool Near(const glm::vec3& a, const glm::vec3& b, float tolerance = 1e-5f)
		{
			return glm::length(a - b) <= tolerance;
		}

		// A unit quaternion as glTF stores it: xyzw
		glm::vec4 Rotation(const glm::vec3& axis, float degrees)
		{
			const glm::quat rotation = glm::angleAxis(glm::radians(degrees), axis);
			return {rotation.x, rotation.y, rotation.z, rotation.w};
		}

		AnimationChannel MakeChannel(AnimationPath path, AnimationInterpolation interpolation, std::vector<float> times,
			std::vector<glm::vec4> values, uint32_t node = 0)
		{
			return {.Node = node,
				.Path = path,
				.Interpolation = interpolation,
				.Times = std::move(times),
				.Values = std::move(values)};
		}

		constexpr UUID SlidingModelId(0x3000000000004000ull, 0x8000000000000002ull);

		// A model with one clip, two seconds long, moving node 0 from the origin to x = 2
		Model MakeSlidingModel()
		{
			Model model;
			model.Nodes.emplace_back();
			model.Animations.push_back({.Name = "Slide",
				.Duration = 2.0f,
				.Channels = {MakeChannel(AnimationPath::Translation, AnimationInterpolation::Linear, {0.0f, 2.0f},
					{glm::vec4(0.0f), glm::vec4(2.0f, 0.0f, 0.0f, 0.0f)})}});
			model.Animations.push_back({.Name = "Short", .Duration = 0.5f});
			return model;
		}

	}

	TEST_CASE("Linear channels interpolate between keyframes, and hold beyond them")
	{
		const AnimationChannel channel =
			MakeChannel(AnimationPath::Translation, AnimationInterpolation::Linear, {1.0f, 2.0f, 4.0f},
				{glm::vec4(0.0f), glm::vec4(10.0f, 0.0f, 0.0f, 0.0f), glm::vec4(10.0f, 4.0f, 0.0f, 0.0f)});

		CHECK(Near(SampleChannel(channel, 1.5f), glm::vec4(5.0f, 0.0f, 0.0f, 0.0f)));
		CHECK(Near(SampleChannel(channel, 3.0f), glm::vec4(10.0f, 2.0f, 0.0f, 0.0f)));
		CHECK(Near(SampleChannel(channel, 2.0f), glm::vec4(10.0f, 0.0f, 0.0f, 0.0f)));
		// Before the first keyframe and after the last
		CHECK(Near(SampleChannel(channel, 0.0f), glm::vec4(0.0f)));
		CHECK(Near(SampleChannel(channel, 9.0f), glm::vec4(10.0f, 4.0f, 0.0f, 0.0f)));
		// A time that isn't a number is the start
		CHECK(Near(SampleChannel(channel, std::numeric_limits<float>::quiet_NaN()), glm::vec4(0.0f)));
	}

	TEST_CASE("Rotations interpolate spherically, the short way round")
	{
		const glm::vec3 up(0.0f, 1.0f, 0.0f);
		const AnimationChannel channel = MakeChannel(AnimationPath::Rotation, AnimationInterpolation::Linear,
			{0.0f, 1.0f}, {Rotation(up, 0.0f), Rotation(up, 90.0f)});

		CHECK(Near(SampleChannel(channel, 0.5f), Rotation(up, 45.0f)));
		CHECK(Near(SampleChannel(channel, 0.25f), Rotation(up, 22.5f)));

		// The same rotation written with the opposite sign is the same rotation: no turn the long way round
		const glm::vec4 negated = -Rotation(up, 90.0f);
		const AnimationChannel flipped = MakeChannel(
			AnimationPath::Rotation, AnimationInterpolation::Linear, {0.0f, 1.0f}, {Rotation(up, 0.0f), negated});
		const glm::vec4 halfway = SampleChannel(flipped, 0.5f);
		CHECK(std::abs(glm::dot(halfway, Rotation(up, 45.0f))) == doctest::Approx(1.0f).epsilon(1e-5));
	}

	TEST_CASE("Step channels hold each keyframe until the next")
	{
		const AnimationChannel channel = MakeChannel(AnimationPath::Scale, AnimationInterpolation::Step, {0.0f, 1.0f},
			{glm::vec4(1.0f, 1.0f, 1.0f, 0.0f), glm::vec4(2.0f, 2.0f, 2.0f, 0.0f)});

		CHECK(Near(SampleChannel(channel, 0.0f), glm::vec4(1.0f, 1.0f, 1.0f, 0.0f)));
		CHECK(Near(SampleChannel(channel, 0.99f), glm::vec4(1.0f, 1.0f, 1.0f, 0.0f)));
		CHECK(Near(SampleChannel(channel, 1.0f), glm::vec4(2.0f, 2.0f, 2.0f, 0.0f)));
	}

	TEST_CASE("Cubic splines follow Hermite curves through their tangents")
	{
		// From 0 at t = 0 to 1 at t = 2, leaving at 1 per second and arriving flat. Tangents are per second: glTF
		// scales them by the time between keyframes
		const glm::vec4 none(0.0f);
		const AnimationChannel channel = MakeChannel(AnimationPath::Translation, AnimationInterpolation::CubicSpline,
			{0.0f, 2.0f},
			{none, glm::vec4(0.0f), glm::vec4(1.0f, 0.0f, 0.0f, 0.0f), none, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f), none});

		// At s = t / 2: h00 p0 + h10 (2 m0) + h01 p1 + h11 (2 m1), with p0 = 0, m0 = 1, p1 = 1, m1 = 0
		for (const float time : {0.5f, 1.0f, 1.5f})
		{
			CAPTURE(time);
			const float s = time / 2.0f;
			const float expected = (s * s * s - 2.0f * s * s + s) * 2.0f + (-2.0f * s * s * s + 3.0f * s * s);
			CHECK(SampleChannel(channel, time).x == doctest::Approx(expected));
		}
		// The keyframes themselves, not their tangents
		CHECK(SampleChannel(channel, 0.0f).x == doctest::Approx(0.0f));
		CHECK(SampleChannel(channel, 2.0f).x == doctest::Approx(1.0f));

		// Rotations on splines stay unit quaternions
		const glm::vec3 axis(0.0f, 0.0f, 1.0f);
		const AnimationChannel turn = MakeChannel(AnimationPath::Rotation, AnimationInterpolation::CubicSpline,
			{0.0f, 1.0f}, {none, Rotation(axis, 0.0f), none, none, Rotation(axis, 90.0f), none});
		CHECK(glm::length(SampleChannel(turn, 0.3f)) == doctest::Approx(1.0f));
		CHECK(Near(SampleChannel(turn, 0.5f), Rotation(axis, 45.0f), 1e-4f));
	}

	TEST_CASE("Keyframes at the same time switch at once, and channels without keyframes don't move anything")
	{
		const AnimationChannel jump =
			MakeChannel(AnimationPath::Translation, AnimationInterpolation::Linear, {0.0f, 1.0f, 1.0f, 2.0f},
				{glm::vec4(0.0f), glm::vec4(1.0f, 0.0f, 0.0f, 0.0f), glm::vec4(5.0f, 0.0f, 0.0f, 0.0f),
					glm::vec4(6.0f, 0.0f, 0.0f, 0.0f)});
		CHECK(SampleChannel(jump, 0.5f).x == doctest::Approx(0.5f));
		CHECK(SampleChannel(jump, 1.5f).x == doctest::Approx(5.5f));

		CHECK(SampleChannel(MakeChannel(AnimationPath::Rotation, AnimationInterpolation::Linear, {}, {}), 1.0f) ==
			glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
		CHECK(SampleChannel(MakeChannel(AnimationPath::Scale, AnimationInterpolation::Step, {}, {}), 1.0f).x == 1.0f);
	}

	TEST_CASE("Clip time wraps when the clip loops, and is held at its ends when it doesn't")
	{
		const AnimationClip clip{.Duration = 2.0f};

		CHECK(GetClipTime(clip, 0.5f, true) == doctest::Approx(0.5f));
		// The end of a looping clip is its last keyframe, not its first
		CHECK(GetClipTime(clip, 2.0f, true) == 2.0f);
		CHECK(GetClipTime(clip, 4.0f, true) == 0.0f);
		CHECK(GetClipTime(clip, 5.0f, true) == doctest::Approx(1.0f));
		CHECK(GetClipTime(clip, -0.5f, true) == doctest::Approx(1.5f));
		CHECK(GetClipTime(clip, 5.0f, false) == doctest::Approx(2.0f));
		CHECK(GetClipTime(clip, -1.0f, false) == doctest::Approx(0.0f));
		CHECK(GetClipTime(clip, std::numeric_limits<float>::infinity(), true) == 0.0f);
		CHECK(GetClipTime(AnimationClip(), 3.0f, true) == 0.0f);
	}

	TEST_CASE("A clip poses only what it animates, of the nodes there are")
	{
		AnimationClip clip;
		clip.Channels.push_back(MakeChannel(
			AnimationPath::Translation, AnimationInterpolation::Step, {0.0f}, {glm::vec4(1.0f, 2.0f, 3.0f, 0.0f)}, 1));
		clip.Channels.push_back(MakeChannel(AnimationPath::Rotation, AnimationInterpolation::Step, {0.0f},
			{Rotation(glm::vec3(1.0f, 0.0f, 0.0f), 90.0f)}, 7));
		std::vector<TransformComponent> pose(2);
		pose[1].Scale = glm::vec3(3.0f);
		pose[1].Rotation = glm::angleAxis(1.0f, glm::vec3(0.0f, 1.0f, 0.0f));

		ApplyClip(clip, 0.0f, pose);

		CHECK(pose[1].Position == glm::vec3(1.0f, 2.0f, 3.0f));
		CHECK(pose[1].Scale == glm::vec3(3.0f));
		CHECK(pose[1].Rotation == glm::angleAxis(1.0f, glm::vec3(0.0f, 1.0f, 0.0f)));
		CHECK(pose[0].Position == glm::vec3(0.0f));
	}

	TEST_CASE("Blended poses are part of the way from one to the other")
	{
		std::vector<TransformComponent> from(1);
		std::vector<TransformComponent> to(1);
		to[0].Position = glm::vec3(4.0f, 0.0f, 0.0f);
		to[0].Rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));

		BlendPose(from, to, 0.25f);

		CHECK(Near(to[0].Position, glm::vec3(1.0f, 0.0f, 0.0f)));
		CHECK(glm::angle(to[0].Rotation) == doctest::Approx(glm::radians(22.5f)));
	}

	TEST_CASE("A crossfade starts the new clip and fades the old one out over its duration")
	{
		AnimatorComponent animator{.Clip = 1, .Time = 0.75f};

		CrossFade(animator, 0, 0.5f);

		CHECK(animator.Clip == 0);
		CHECK(animator.Time == 0.0f);
		CHECK(animator.FadeClip == 1);
		CHECK(animator.FadeTime == 0.75f);
		CHECK(GetFadeWeight(animator, 0.0f) == 0.0f);
		CHECK(GetFadeWeight(animator, 0.25f) == doctest::Approx(0.5f));
		CHECK(GetFadeWeight(animator, 1.0f) == 1.0f);

		// Advancing past the fade ends it
		const Model model = MakeSlidingModel();
		AdvanceAnimator(animator, 0.3f, &model);
		CHECK(animator.FadeDuration == 0.5f);
		CHECK(animator.FadeElapsed == doctest::Approx(0.3f));
		CHECK(animator.FadeTime == doctest::Approx(0.05f));
		AdvanceAnimator(animator, 0.3f, &model);
		CHECK(animator.FadeDuration == 0.0f);
		CHECK(GetFadeWeight(animator, 0.0f) == 1.0f);
		CHECK(animator.Time == doctest::Approx(0.6f));

		// No duration switches at once; Play does too
		CrossFade(animator, 1, 0.0f);
		CHECK(animator.Clip == 1);
		CHECK(animator.FadeDuration == 0.0f);
		animator.Time = 0.4f;
		CrossFade(animator, 0, 1.0f);
		Play(animator, 1);
		CHECK(animator.Clip == 1);
		CHECK(animator.Time == 0.0f);
		CHECK(animator.FadeDuration == 0.0f);
	}

	TEST_CASE("Animators advance by their speed, loop, and stop at the end of clips that don't loop")
	{
		const Model model = MakeSlidingModel();
		AnimatorComponent looping;
		AdvanceAnimator(looping, 1.5f, &model);
		AdvanceAnimator(looping, 1.0f, &model);
		CHECK(looping.Time == doctest::Approx(0.5f));
		CHECK(looping.Playing);

		AnimatorComponent once{.Speed = 2.0f, .Loop = false};
		AdvanceAnimator(once, 0.75f, &model);
		CHECK(once.Time == doctest::Approx(1.5f));
		AdvanceAnimator(once, 0.75f, &model);
		CHECK(once.Time == doctest::Approx(2.0f));
		CHECK_FALSE(once.Playing);
		AdvanceAnimator(once, 1.0f, &model);
		CHECK(once.Time == doctest::Approx(2.0f));

		AnimatorComponent backwards{.Time = 0.5f, .Speed = -1.0f, .Loop = false};
		AdvanceAnimator(backwards, 1.0f, &model);
		CHECK(backwards.Time == 0.0f);
		CHECK_FALSE(backwards.Playing);

		// Paused animators keep still, and without their model time just advances
		AnimatorComponent paused{.Time = 0.3f, .Playing = false};
		AdvanceAnimator(paused, 1.0f, &model);
		CHECK(paused.Time == 0.3f);
		AnimatorComponent unknown;
		AdvanceAnimator(unknown, 5.0f, nullptr);
		CHECK(unknown.Time == doctest::Approx(5.0f));
		// Nor with a clip the model doesn't have
		AnimatorComponent missing{.Clip = 9};
		AdvanceAnimator(missing, 3.0f, &model);
		CHECK(missing.Time == doctest::Approx(3.0f));
	}

	TEST_CASE("The animation system advances every animator in the scene with its instance's model")
	{
		const Model model = MakeSlidingModel();
		Scene scene;
		Entity known = scene.CreateEntity("Known");
		known.Add<ModelInstanceComponent>(ModelInstanceComponent{.Model = SlidingModelId});
		known.Add<AnimatorComponent>();
		Entity orphan = scene.CreateEntity("Orphan");
		orphan.Add<AnimatorComponent>();

		AdvanceAnimators(
			scene, 2.5f, [&model](UUID id) -> const Model* { return id == SlidingModelId ? &model : nullptr; });

		CHECK(known.Get<AnimatorComponent>().Time == doctest::Approx(0.5f));
		CHECK(orphan.Get<AnimatorComponent>().Time == doctest::Approx(2.5f));
		// Without models, time just advances
		AdvanceAnimators(scene, 1.0f, {});
		CHECK(known.Get<AnimatorComponent>().Time == doctest::Approx(1.5f));
	}

}
