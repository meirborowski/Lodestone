#pragma once

#include "Lodestone/Core/UUID.h"

#include <cstdint>

// The components that tie entities to the models they were placed from, and animate them (see
// docs/Decisions/0020-skeletal-animation.md). They're data only, registered in RegisterAnimationComponents(): the
// simulation advances animators, and ScenePose derives the poses they make

namespace Lodestone {

	class ComponentRegistry;

	// On the root of a model's entities (see BuildModelEntities()): the model they were placed from. The entities below
	// it with a ModelNode are its nodes, down to any root of another instance
	struct ModelInstanceComponent
	{
		// The model asset
		UUID Model;
	};

	// On an entity placed from a node of a model: which node, so animation clips and skins can find it
	struct ModelNodeComponent
	{
		// Into the model's nodes
		uint32_t Node = 0;
	};

	// On an entity with a MeshRenderer: skins the mesh with a skin of the model, whose joints are the instance's nodes.
	// The entity's own transform doesn't move a skinned mesh; its joints do
	struct SkinComponent
	{
		// Into the model's skins
		uint32_t Skin = 0;
	};

	// On the root of a model instance: plays the model's animation clips on its nodes, with crossfades between them.
	// It's state only - the simulation advances it, and the pose is derived from it where it's needed (ScenePose)
	struct AnimatorComponent
	{
		// The playing clip, into the model's animations
		uint32_t Clip = 0;
		// How far into the clip, in seconds
		float Time = 0.0f;
		// How fast time advances: 1 is normal, negative plays backwards
		float Speed = 1.0f;
		bool Playing = true;
		// Whether the clip starts over when it ends; otherwise it stops there
		bool Loop = true;

		// A crossfade from another clip, which plays on, fading out, as Clip fades in (see CrossFade())
		uint32_t FadeClip = 0;
		float FadeTime = 0.0f;
		// How long the fade lasts, in seconds: 0 when there's none
		float FadeDuration = 0.0f;
		// How far into the fade, in seconds
		float FadeElapsed = 0.0f;
	};

	// Registers the components above
	void RegisterAnimationComponents(ComponentRegistry& registry);

}
