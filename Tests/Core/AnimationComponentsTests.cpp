#include "Lodestone/Animation/AnimationComponents.h"

#include "Common/DescribeError.h"
#include "Lodestone/Animation/Animation.h"
#include "Lodestone/Reflection/ComponentRegistry.h"
#include "Lodestone/Scene/Entity.h"
#include "Lodestone/Scene/SceneSerializer.h"
#include "Lodestone/Simulation/Simulation.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

namespace Lodestone {

	TEST_CASE("The animation components are registered with the engine, with defaults within their limits")
	{
		const ComponentRegistry& registry = GetEngineComponentRegistry();
		for (const char* name : {"ModelInstance", "ModelNode", "Skin", "Animator"})
		{
			CAPTURE(name);
			const ComponentType* type = registry.Find(name);
			REQUIRE(type != nullptr);
			CHECK_FALSE(type->IsRequired());
			CHECK_FALSE(type->IsInternal());
			CHECK_FALSE(type->GetDescription().empty());
			for (const FieldInfo& field : type->GetFields())
			{
				CAPTURE(field.GetName());
				CHECK_FALSE(field.GetDescription().empty());
				CHECK(field.Validate(field.GetDefault()).has_value());
			}
		}
		CHECK(registry.Find<AnimatorComponent>() == registry.Find("Animator"));
		CHECK(registry.Find<ModelInstanceComponent>()->FindField("Model")->GetType() == FieldType::UUID);

		// What clients need replicates; the crossfade only the engine changes
		const ComponentType& animator = *registry.Find("Animator");
		for (const char* name : {"Clip", "Time", "Speed", "Playing", "Loop"})
		{
			CAPTURE(name);
			REQUIRE(animator.FindField(name) != nullptr);
			CHECK(animator.FindField(name)->IsReplicated());
			CHECK_FALSE(animator.FindField(name)->IsReadOnly());
		}
		for (const char* name : {"FadeClip", "FadeTime", "FadeDuration", "FadeElapsed"})
		{
			CAPTURE(name);
			REQUIRE(animator.FindField(name) != nullptr);
			CHECK(animator.FindField(name)->IsReplicated());
			CHECK(animator.FindField(name)->IsReadOnly());
		}
	}

	TEST_CASE("Scenes save and load their animation components")
	{
		Scene scene;
		Entity root = scene.CreateEntity("Character");
		const UUID modelAsset = UUID::Generate();
		root.Add<ModelInstanceComponent>(ModelInstanceComponent{.Model = modelAsset});
		AnimatorComponent animator{.Clip = 2, .Time = 0.75f, .Speed = -0.5f, .Playing = false, .Loop = false};
		CrossFade(animator, 1, 0.4f);
		animator.FadeElapsed = 0.1f;
		root.Add<AnimatorComponent>(animator);
		Entity body = scene.CreateEntity("Body");
		body.Add<ModelNodeComponent>(ModelNodeComponent{.Node = 3});
		body.Add<SkinComponent>(SkinComponent{.Skin = 1});

		const std::string text = SceneSerializer::SerializeToText(scene);
		const auto loaded = SceneSerializer::DeserializeFromText(text);

		REQUIRE_MESSAGE(loaded.has_value(), Testing::DescribeError(loaded));
		CHECK(SceneSerializer::SerializeToText(**loaded) == text);
		const Entity loadedRoot = (*loaded)->FindEntity(root.GetId());
		CHECK(loadedRoot.Get<ModelInstanceComponent>().Model == modelAsset);
		const auto& loadedAnimator = loadedRoot.Get<AnimatorComponent>();
		CHECK(loadedAnimator.Clip == 1);
		CHECK(loadedAnimator.FadeClip == 2);
		CHECK(loadedAnimator.FadeTime == 0.75f);
		CHECK(loadedAnimator.FadeDuration == 0.4f);
		CHECK(loadedAnimator.FadeElapsed == 0.1f);
		CHECK(loadedAnimator.Speed == -0.5f);
		CHECK_FALSE(loadedAnimator.Loop);
		const Entity loadedBody = (*loaded)->FindEntity(body.GetId());
		CHECK(loadedBody.Get<ModelNodeComponent>().Node == 3);
		CHECK(loadedBody.Get<SkinComponent>().Skin == 1);
	}

	TEST_CASE("Animator fields outside their limits fail to load")
	{
		Scene scene;
		scene.CreateEntity("Character").Add<AnimatorComponent>();
		std::string text = SceneSerializer::SerializeToText(scene);
		const std::string field = R"("Speed": 1)";
		const size_t position = text.find(field);
		REQUIRE(position != std::string::npos);
		text.replace(position, field.size(), R"("Speed": 500)");

		CHECK_FALSE(SceneSerializer::DeserializeFromText(text).has_value());
	}

	TEST_CASE("Animators restored from a snapshot advance exactly as they did")
	{
		const auto state = [](Scene& scene, UUID id)
		{
			const AnimatorComponent& animator = scene.FindEntity(id).Get<AnimatorComponent>();
			return std::vector<float>{static_cast<float>(animator.Clip), animator.Time, animator.Speed,
				animator.Playing ? 1.0f : 0.0f, static_cast<float>(animator.FadeClip), animator.FadeTime,
				animator.FadeDuration, animator.FadeElapsed};
		};
		Scene scene;
		Entity character = scene.CreateEntity("Character");
		character.Add<AnimatorComponent>(AnimatorComponent{.Speed = 1.25f});
		Simulation simulation(scene, {.TickRate = 60});
		uint64_t crossFadeTick = 0;
		simulation.AddSystem("Gameplay",
			[&crossFadeTick](SimulationContext& context)
			{
				// A crossfade partway through, as gameplay would start one
				if (context.GetTick() == crossFadeTick)
				{
					for (const auto [handle, animator] :
						context.GetScene().GetRegistry().view<AnimatorComponent>().each())
						CrossFade(animator, 1, 0.5f);
				}
			});
		simulation.AddSystem("Animation",
			[](SimulationContext& context) { AdvanceAnimators(context.GetScene(), context.GetDeltaTime(), {}); });

		for (int tick = 0; tick < 20; ++tick)
			simulation.Step({});
		const SimulationState saved = simulation.SaveState();
		crossFadeTick = 30;
		for (int tick = 0; tick < 40; ++tick)
			simulation.Step({});
		const std::vector<float> first = state(scene, character.GetId());
		CHECK(first[1] > 0.0f);

		simulation.RestoreState(saved);
		for (int tick = 0; tick < 40; ++tick)
			simulation.Step({});
		CHECK(state(scene, character.GetId()) == first);
	}

}
