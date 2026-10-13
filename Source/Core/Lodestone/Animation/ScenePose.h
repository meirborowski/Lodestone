#pragma once

#include "Lodestone/Animation/AnimationComponents.h"
#include "Lodestone/Asset/Model.h"
#include "Lodestone/Core/UUID.h"
#include "Lodestone/Scene/Components.h"

#include <entt/entity/entity.hpp>
#include <glm/mat4x4.hpp>

#include <optional>
#include <unordered_map>
#include <vector>

namespace Lodestone {

	class Scene;

	// An animator drawn in another state than it has: the editor's preview of a clip
	struct AnimatorPreview
	{
		// The entity with the Animator
		UUID Entity;
		uint32_t Clip = 0;
		float Time = 0.0f;
	};

	// How a scene is posed
	struct PoseOptions
	{
		// How far the view is from the start of the latest simulation tick to its end
		// (Simulation::GetInterpolationFactor()): transforms are interpolated, and animators drawn this far through it
		float InterpolationFactor = 1.0f;
		// The simulation's tick, in seconds (Simulation::GetTickDuration()); 0 outside a simulation
		float TickDuration = 0.0f;
		// The models animators play and meshes are skinned with. Without one, or a model it doesn't have, nothing of
		// that model is animated or skinned
		ModelLookup Models;
		std::optional<AnimatorPreview> Preview;
	};

	// Where a scene's entities are drawn at a moment: transforms interpolated between ticks, and the nodes of model
	// instances moved by their animators' clips, crossfades blended. Poses are derived from animator state rather
	// than kept in the scene (see docs/Decisions/0020-skeletal-animation.md), so whatever draws, picks or hits entities
	// asks a ScenePose where they are. Children of animated nodes follow them.
	//
	// Built for one moment and thrown away; it reads the scene when it's built and when asked, so the scene mustn't
	// change in between. Not thread-safe: it caches what it works out
	class ScenePose
	{
	public:
		explicit ScenePose(const Scene& scene, PoseOptions options = {});

		const Scene& GetScene() const { return *m_Scene; }
		const PoseOptions& GetOptions() const { return m_Options; }

		// The entity's transform relative to its parent, as drawn
		TransformComponent GetLocalTransform(entt::entity entity) const;
		// The entity's transform relative to the world, as drawn
		glm::mat4 GetWorldMatrix(entt::entity entity) const;
		// For an entity that draws a skinned mesh, a matrix for each joint of its skin, from the mesh's bind pose to
		// the world; a vertex is drawn at the weighted sum of its joints' matrices times its position. Empty if the
		// entity isn't skinned, or its model, skin or any joint is missing, or the skin has too few joints for the mesh
		// - it's then drawn unskinned, at its own transform
		std::vector<glm::mat4> GetJointMatrices(entt::entity entity) const;

		// Whether an entity is moved by an animator
		bool IsAnimated(entt::entity entity) const { return m_Animated.contains(entity); }

	private:
		// A model instance: its model, and the entity of each of its nodes (null where there's none)
		struct Instance
		{
			const Model* Asset = nullptr;
			std::vector<entt::entity> Nodes;
		};

		const Instance& GetInstance(entt::entity root) const;
		// The root of the model instance an entity is part of: itself or its nearest ancestor with a ModelInstance
		entt::entity FindInstanceRoot(entt::entity entity) const;
		void PoseAnimator(entt::entity root, const AnimatorComponent& animator);

	private:
		const Scene* m_Scene;
		PoseOptions m_Options;
		// How far behind its latest time each animator is drawn, in seconds
		float m_SecondsBehind;
		std::unordered_map<entt::entity, TransformComponent> m_Animated;
		mutable std::unordered_map<entt::entity, Instance> m_Instances;
		mutable std::unordered_map<entt::entity, glm::mat4> m_WorldMatrices;
	};

}
