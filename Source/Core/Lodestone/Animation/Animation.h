#pragma once

#include "Lodestone/Animation/AnimationComponents.h"
#include "Lodestone/Asset/Model.h"
#include "Lodestone/Scene/Components.h"

#include <glm/vec4.hpp>

#include <cstdint>
#include <span>

// Sampling animation clips, and controlling animators (see docs/Decisions/0020-skeletal-animation.md)

namespace Lodestone {

	class Scene;

	// Where in a clip an animator's time falls: times within the clip, its end included, as they are; times beyond it
	// wrapped into it when it loops, and held at its ends when it doesn't. Times that aren't finite are the clip's
	// start
	float GetClipTime(const AnimationClip& clip, float time, bool loop);

	// A channel's value at a time: xyz for translations and scales, a unit xyzw quaternion for rotations. The first
	// keyframe's value holds before it, and the last one's after it
	glm::vec4 SampleChannel(const AnimationChannel& channel, float time);

	// Poses a model's nodes with a clip at a time: pose[node] is a node's local transform, and the clip sets the
	// properties it animates, leaving the rest. Channels of nodes beyond the pose are left out
	void ApplyClip(const AnimationClip& clip, float time, std::span<TransformComponent> pose);

	// Blends one pose into another node by node: each node of `to` becomes the transform `weight` of the way from
	// `from` to it - 0 gives `from`, 1 leaves `to`. The poses are the same size
	void BlendPose(std::span<const TransformComponent> from, std::span<TransformComponent> to, float weight);

	// The playing clip's share of a crossfade at a time into it, from 0 (only the clip fading out shows) to 1 (only the
	// playing clip). 1 when there's no crossfade
	float GetFadeWeight(const AnimatorComponent& animator, float elapsed);

	// Plays a clip from its start at once, ending any crossfade
	void Play(AnimatorComponent& animator, uint32_t clip);
	// Fades from what's playing to a clip, which plays from its start, over a duration in seconds; 0 or less switches
	// at once. A crossfade under way is cut short: its outgoing clip stops showing
	void CrossFade(AnimatorComponent& animator, uint32_t clip, float duration);

	// Advances an animator by some seconds, if it's playing. Given its model, looping time wraps into the clip, a clip
	// that doesn't loop stops at its end (and the animator stops playing), and a clip fading out loops while it fades;
	// without it, time just advances, which poses the same
	void AdvanceAnimator(AnimatorComponent& animator, float deltaTime, const Model* model);
	// Advances every animator in a scene by a tick: the simulation's animation system. Each animator's model is its
	// entity's ModelInstance's
	void AdvanceAnimators(Scene& scene, float deltaTime, const ModelLookup& models);

	// The animator that moves an entity: its own, or its nearest ancestor's. The nil UUID if there's none, or the
	// entity isn't in the scene
	UUID FindAnimator(const Scene& scene, UUID entity);

}
