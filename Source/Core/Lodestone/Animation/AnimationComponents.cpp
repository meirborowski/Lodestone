#include "Lodestone/Animation/AnimationComponents.h"

#include "Lodestone/Reflection/ComponentRegistry.h"

namespace Lodestone {

	void RegisterAnimationComponents(ComponentRegistry& registry)
	{
		registry
			.Register<ModelInstanceComponent>("ModelInstance",
				"The root of entities placed from a model: the entities below it with a ModelNode are its nodes")
			.Field("Model", &ModelInstanceComponent::Model, {.Description = "The model asset's UUID"});

		registry.Register<ModelNodeComponent>("ModelNode", "Which node of its model instance's model the entity is")
			.Field("Node", &ModelNodeComponent::Node, {.Description = "Into the model's nodes, from 0"});

		registry
			.Register<SkinComponent>(
				"Skin", "Skins the entity's mesh with a skin of its model, whose joints are the instance's nodes")
			.Field("Skin", &SkinComponent::Skin, {.Description = "Into the model's skins, from 0"});

		constexpr auto replicated = FieldFlags::Replicated;
		constexpr auto engineOnly = FieldFlags::Replicated | FieldFlags::ReadOnly;
		registry
			.Register<AnimatorComponent>(
				"Animator", "Plays the animation clips of the model instance it's on, with crossfades between them")
			.Field("Clip", &AnimatorComponent::Clip,
				{.Description = "The playing clip, into the model's animations from 0", .Flags = replicated})
			.Field("Time", &AnimatorComponent::Time,
				{.Description = "How far into the clip, in seconds", .Min = -1.0e6, .Max = 1.0e6, .Flags = replicated})
			.Field("Speed", &AnimatorComponent::Speed,
				{.Description = "How fast time advances: 1 is normal, negative plays backwards",
					.Min = -100.0,
					.Max = 100.0,
					.Flags = replicated})
			.Field(
				"Playing", &AnimatorComponent::Playing, {.Description = "Whether time advances", .Flags = replicated})
			.Field("Loop", &AnimatorComponent::Loop,
				{.Description = "Whether the clip starts over when it ends; otherwise it stops there",
					.Flags = replicated})
			.Field("FadeClip", &AnimatorComponent::FadeClip,
				{.Description = "The clip fading out during a crossfade", .Flags = engineOnly})
			.Field("FadeTime", &AnimatorComponent::FadeTime,
				{.Description = "How far into the clip fading out, in seconds", .Flags = engineOnly})
			.Field("FadeDuration", &AnimatorComponent::FadeDuration,
				{.Description = "How long the crossfade lasts, in seconds: 0 when there's none", .Flags = engineOnly})
			.Field("FadeElapsed", &AnimatorComponent::FadeElapsed,
				{.Description = "How far into the crossfade, in seconds", .Flags = engineOnly});
	}

}
