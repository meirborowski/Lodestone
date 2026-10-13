#include "Lodestone/Animation/ScenePose.h"

#include "Common/DescribeError.h"
#include "Lodestone/Animation/Animation.h"
#include "Lodestone/Asset/ModelImporter.h"
#include "Lodestone/Scene/Entity.h"
#include "Lodestone/Scene/EntitySerializer.h"
#include "Lodestone/Scene/ModelEntities.h"
#include "Lodestone/Scene/Scene.h"

#include <doctest/doctest.h>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <cmath>
#include <filesystem>

namespace Lodestone {

	namespace {

		constexpr UUID ArmId(0x3000000000004000ull, 0x8000000000000003ull);
		// SkinnedArm.glb's clips
		constexpr uint32_t Bend = 0;
		constexpr uint32_t Wave = 1;

		const Model& GetArm()
		{
			static const Model Arm = []
			{
				auto imported =
					ModelImporter::Import(std::filesystem::path(LS_TEST_FIXTURE_DIR) / "Models" / "SkinnedArm.glb");
				REQUIRE_MESSAGE(imported.has_value(), Testing::DescribeError(imported));
				return std::move(*imported);
			}();
			return Arm;
		}

		const Model* FindArm(UUID id)
		{
			return id == ArmId ? &GetArm() : nullptr;
		}

		bool Near(const glm::vec3& a, const glm::vec3& b, float tolerance = 1e-4f)
		{
			return glm::length(a - b) <= tolerance;
		}

		// The arm placed in a scene: its root (with the animator), the skinned mesh, and its two joints
		struct ArmScene
		{
			ArmScene()
			{
				auto placed = EntitySerializer::InstantiateTree(
					World, BuildModelEntities(GetArm(), ArmId, "Arm"), EntitySerializer::IdPolicy::Regenerate);
				REQUIRE_MESSAGE(placed.has_value(), Testing::DescribeError(placed));
				Root = *placed;
				Mesh = World.FindEntity(Root.Get<HierarchyComponent>().Children[0]);
				Lower = World.FindEntity(Root.Get<HierarchyComponent>().Children[1]);
				Upper = World.FindEntity(Lower.Get<HierarchyComponent>().Children[0]);
			}

			AnimatorComponent& GetAnimator() { return Root.Get<AnimatorComponent>(); }

			ScenePose Pose(PoseOptions options = {}) const
			{
				options.Models = FindArm;
				return ScenePose(World, std::move(options));
			}

			Scene World;
			Entity Root;
			Entity Mesh;
			Entity Lower;
			Entity Upper;
		};

		float GetAngleAboutZ(const glm::quat& rotation)
		{
			return glm::degrees(2.0f * std::atan2(rotation.z, rotation.w));
		}

	}

	TEST_CASE("Animators pose their instance's nodes, leaving the scene's transforms as they are")
	{
		ArmScene arm;
		arm.GetAnimator().Time = 0.5f;

		const ScenePose pose = arm.Pose();

		// Bend turns Upper about Z: halfway to 90 degrees half a second in
		CHECK(GetAngleAboutZ(pose.GetLocalTransform(arm.Upper.GetHandle()).Rotation) == doctest::Approx(45.0f));
		CHECK(pose.IsAnimated(arm.Upper.GetHandle()));
		CHECK_FALSE(pose.IsAnimated(arm.Lower.GetHandle()));
		// What the clip doesn't animate is the scene's
		CHECK(pose.GetLocalTransform(arm.Upper.GetHandle()).Position == glm::vec3(0.0f, 1.0f, 0.0f));
		CHECK(arm.Upper.GetTransform().Rotation == glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f));

		// Without the model, nothing moves
		const ScenePose unknown(arm.World);
		CHECK(unknown.GetLocalTransform(arm.Upper.GetHandle()).Rotation == glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f));
	}

	TEST_CASE("Children of animated nodes follow them")
	{
		ArmScene arm;
		arm.GetAnimator().Time = 1.0f;
		arm.GetAnimator().Loop = false;
		Entity hand = arm.World.CreateEntity("Hand");
		hand.GetTransform().Position = glm::vec3(0.0f, 1.0f, 0.0f);
		REQUIRE(arm.World.SetParent(hand, arm.Upper).has_value());

		const ScenePose pose = arm.Pose();

		// Upper is at y = 1, turned 90 degrees about Z, so its +Y points along -X
		CHECK(Near(glm::vec3(pose.GetWorldMatrix(hand.GetHandle())[3]), glm::vec3(-1.0f, 1.0f, 0.0f)));
		CHECK(Near(glm::vec3(arm.World.GetWorldMatrix(hand)[3]), glm::vec3(0.0f, 2.0f, 0.0f)));
	}

	TEST_CASE("Joint matrices skin a mesh from its bind pose to where its joints are")
	{
		ArmScene arm;
		arm.GetAnimator().Time = 0.5f;
		arm.Root.GetTransform().Position = glm::vec3(3.0f, 0.0f, 0.0f);

		const ScenePose pose = arm.Pose();
		const std::vector<glm::mat4> joints = pose.GetJointMatrices(arm.Mesh.GetHandle());

		REQUIRE(joints.size() == 2);
		// The column's foot moves with Lower, which only the root moves
		CHECK(Near(glm::vec3(joints[0] * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)), glm::vec3(3.0f, 0.0f, 0.0f)));
		// Its top moves with Upper: a meter above the bend, turned 45 degrees about Z
		const float half = std::sqrt(0.5f);
		CHECK(
			Near(glm::vec3(joints[1] * glm::vec4(0.0f, 2.0f, 0.0f, 1.0f)), glm::vec3(3.0f - half, 1.0f + half, 0.0f)));

		// Entities that aren't skinned, or whose joints are gone, have none
		CHECK(pose.GetJointMatrices(arm.Upper.GetHandle()).empty());
		arm.World.DestroyEntity(arm.Upper);
		CHECK(arm.Pose().GetJointMatrices(arm.Mesh.GetHandle()).empty());
	}

	TEST_CASE("Between ticks, animators are drawn behind their latest time")
	{
		ArmScene arm;
		arm.GetAnimator().Time = 0.5f;

		// A tenth of a second a tick, a quarter of the way through the next: drawn 0.075 s behind
		const ScenePose pose = arm.Pose({.InterpolationFactor = 0.25f, .TickDuration = 0.1f});
		CHECK(
			GetAngleAboutZ(pose.GetLocalTransform(arm.Upper.GetHandle()).Rotation) == doctest::Approx(90.0f * 0.425f));

		// A paused animator isn't moving, so it's drawn where it is
		arm.GetAnimator().Playing = false;
		const ScenePose paused = arm.Pose({.InterpolationFactor = 0.25f, .TickDuration = 0.1f});
		CHECK(GetAngleAboutZ(paused.GetLocalTransform(arm.Upper.GetHandle()).Rotation) == doctest::Approx(45.0f));
	}

	TEST_CASE("A preview draws an animator with another clip and time, without changing it")
	{
		ArmScene arm;
		arm.GetAnimator().Time = 0.25f;

		const ScenePose pose =
			arm.Pose({.Preview = AnimatorPreview{.Entity = arm.Root.GetId(), .Clip = Wave, .Time = 0.5f}});

		// Wave nods Upper 45 degrees about X at half a second, and lifts Lower
		const glm::quat nod = pose.GetLocalTransform(arm.Upper.GetHandle()).Rotation;
		CHECK(glm::degrees(glm::angle(nod)) == doctest::Approx(45.0f));
		CHECK(std::abs(nod.x) > 0.3f);
		CHECK(pose.GetLocalTransform(arm.Lower.GetHandle()).Position == glm::vec3(0.0f, 0.25f, 0.0f));
		CHECK(arm.GetAnimator().Clip == Bend);
		CHECK(arm.GetAnimator().Time == 0.25f);
	}

	TEST_CASE("Crossfades blend the clip fading out into the one fading in")
	{
		ArmScene arm;
		AnimatorComponent& animator = arm.GetAnimator();
		animator.Loop = false;
		// Bend at 72 degrees fading out, Bend at its start (no turn) fading in, halfway through
		animator.Time = 0.8f;
		CrossFade(animator, Bend, 1.0f);
		animator.FadeElapsed = 0.5f;

		const ScenePose pose = arm.Pose();

		CHECK(GetAngleAboutZ(pose.GetLocalTransform(arm.Upper.GetHandle()).Rotation) == doctest::Approx(36.0f));
	}

}
